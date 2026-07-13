#include "parser.hpp"

#include "diagnostic.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <algorithm>

namespace {
void appendUtf8(std::string& output, std::uint32_t value) {
    if (value <= 0x7FU) output.push_back(static_cast<char>(value));
    else if (value <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else if (value <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
}

std::pair<std::uint32_t, std::size_t> decodeUtf8(std::string_view text, std::size_t offset) {
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80) return {first, 1};
    const std::size_t length = (first & 0xE0) == 0xC0 ? 2 :
        (first & 0xF0) == 0xE0 ? 3 : (first & 0xF8) == 0xF0 ? 4 : 0;
    if (length == 0 || offset + length > text.size()) return {0, 0};
    std::uint32_t value = first & ((1U << (7U - static_cast<unsigned>(length))) - 1U);
    for (std::size_t i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(text[offset + i]);
        if ((next & 0xC0) != 0x80) return {0, 0};
        value = (value << 6U) | (next & 0x3FU);
    }
    const std::uint32_t minimum = length == 2 ? 0x80U : length == 3 ? 0x800U : 0x10000U;
    if (value < minimum || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU))
        return {0, 0};
    return {value, length};
}

std::string decodeString(const Token& token) {
    const std::string_view body = std::string_view(token.text).substr(1, token.text.size() - 2);
    std::string output;
    for (std::size_t i = 0; i < body.size();) {
        if (body[i] != '\\') {
            const auto [value, length] = decodeUtf8(body, i);
            if (length == 0)
                throw CompileError(token.location, "séquence UTF-8 invalide dans le littéral String");
            appendUtf8(output, value);
            i += length;
            continue;
        }
        if (++i >= body.size())
            throw CompileError(token.location, "échappement incomplet dans le littéral String");
        const char escape = body[i++];
        if (escape == 'n') output.push_back('\n');
        else if (escape == 'r') output.push_back('\r');
        else if (escape == 't') output.push_back('\t');
        else if (escape == '0') output.push_back('\0');
        else if (escape == '\\') output.push_back('\\');
        else if (escape == '"') output.push_back('"');
        else if (escape == 'u' && i < body.size() && body[i] == '{') {
            const std::size_t digitsBegin = ++i;
            while (i < body.size() && body[i] != '}') ++i;
            std::uint32_t value = 0;
            const auto result = std::from_chars(body.data() + digitsBegin, body.data() + i,
                                                value, 16);
            if (digitsBegin == i || i == body.size() || result.ec != std::errc{} ||
                result.ptr != body.data() + i || value > 0x10FFFFU ||
                (value >= 0xD800U && value <= 0xDFFFU))
                throw CompileError(token.location, "échappement Unicode invalide dans le littéral String");
            ++i;
            appendUtf8(output, value);
        } else {
            throw CompileError(token.location, "échappement inconnu dans le littéral String");
        }
    }
    return output;
}

std::uint32_t decodeCharacter(const Token& token) {
    const std::string_view text(token.text);
    const std::string_view body = text.substr(1, text.size() - 2);
    std::uint32_t value = 0;
    std::size_t consumed = 0;
    if (!body.empty() && body[0] == '\\') {
        if (body == "\\n") return '\n';
        if (body == "\\r") return '\r';
        if (body == "\\t") return '\t';
        if (body == "\\0") return 0;
        if (body == "\\\\") return '\\';
        if (body == "\\\'") return '\'';
        if (body.size() >= 5 && body.substr(0, 3) == "\\u{" && body.back() == '}') {
            const std::string_view digits = body.substr(3, body.size() - 4);
            const auto result = std::from_chars(digits.data(), digits.data() + digits.size(),
                                                value, 16);
            if (!digits.empty() && result.ec == std::errc{} &&
                result.ptr == digits.data() + digits.size()) consumed = body.size();
        }
    } else if (!body.empty()) {
        const auto first = static_cast<unsigned char>(body[0]);
        if (first < 0x80) { value = first; consumed = 1; }
        else {
            const std::size_t length = (first & 0xE0) == 0xC0 ? 2 :
                (first & 0xF0) == 0xE0 ? 3 : (first & 0xF8) == 0xF0 ? 4 : 0;
            if (length != 0 && body.size() >= length) {
                value = first & ((1U << (7U - static_cast<unsigned>(length))) - 1U);
                consumed = length;
                for (std::size_t i = 1; i < length; ++i) {
                    const auto next = static_cast<unsigned char>(body[i]);
                    if ((next & 0xC0) != 0x80) { consumed = 0; break; }
                    value = (value << 6U) | (next & 0x3FU);
                }
                const std::uint32_t minimum = length == 2 ? 0x80U :
                    length == 3 ? 0x800U : 0x10000U;
                if (consumed != 0 && value < minimum) consumed = 0;
            }
        }
    }
    if (body.empty() || consumed != body.size() || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU))
        throw CompileError(token.location, "le littéral doit contenir un seul point de code Unicode valide");
    return value;
}
}

const Token& Parser::peek() const { return tokens_[current_]; }
const Token& Parser::previous() const { return tokens_[current_ - 1]; }
bool Parser::check(TokenKind kind) const { return peek().kind == kind; }
bool Parser::match(TokenKind kind) {
    if (!check(kind)) return false;
    ++current_;
    return true;
}
bool Parser::checkSeparator() const {
    return check(TokenKind::Separator) || check(TokenKind::Semicolon);
}
bool Parser::matchSeparator() {
    return match(TokenKind::Separator) || match(TokenKind::Semicolon);
}
bool Parser::startsAssignment() const {
    if (!check(TokenKind::Identifier) || current_ + 1 >= tokens_.size()) return false;
    if (tokens_[current_ + 1].kind == TokenKind::Equal) return true;
    if (current_ + 3 < tokens_.size() && tokens_[current_ + 1].kind == TokenKind::Dot &&
        tokens_[current_ + 2].kind == TokenKind::Identifier &&
        tokens_[current_ + 3].kind == TokenKind::Equal) return true;
    std::size_t cursor = current_ + 1;
    while (cursor < tokens_.size() && tokens_[cursor].kind == TokenKind::LeftBracket) {
        std::size_t depth = 0;
        do {
            if (tokens_[cursor].kind == TokenKind::LeftBracket) ++depth;
            else if (tokens_[cursor].kind == TokenKind::RightBracket) --depth;
            ++cursor;
        } while (cursor < tokens_.size() && depth != 0);
    }
    return cursor < tokens_.size() && tokens_[cursor].kind == TokenKind::Equal;
}

const Token& Parser::consume(TokenKind kind, const std::string& message) {
    if (check(kind)) return tokens_[current_++];
    throw CompileError(peek().location, message + ", reçu " + tokenName(peek().kind));
}

ValueType Parser::consumeType(const std::string& message) {
    if (check(TokenKind::Identifier) && activeTypeParameters_.contains(peek().text)) {
        const std::string name = tokens_[current_++].text;
        return ValueType(ValueType::Kind::TypeParameter, name);
    }
    if (match(TokenKind::Ampersand)) {
        const bool mutableBorrow = match(TokenKind::Mut);
        const ValueType referenced = consumeType("type référencé attendu après '&'");
        return ValueType(std::make_shared<ValueType>(referenced), mutableBorrow);
    }
    if (match(TokenKind::LeftBracket)) {
        const ValueType element = consumeType("type d'élément attendu après '['");
        consume(TokenKind::Semicolon, "';' attendu après le type d'élément");
        const Token& lengthToken = consume(TokenKind::Integer,
                                           "taille entière attendue après ';'");
        std::size_t length = 0;
        const auto result = std::from_chars(lengthToken.text.data(),
            lengthToken.text.data() + lengthToken.text.size(), length);
        if (result.ec != std::errc{} || result.ptr != lengthToken.text.data() + lengthToken.text.size())
            throw CompileError(lengthToken.location, "taille de tableau invalide");
        consume(TokenKind::RightBracket, "']' attendue après la taille du tableau");
        return ValueType(std::make_shared<ValueType>(element), length);
    }
    if (match(TokenKind::SliceType) || match(TokenKind::SliceMutType)) {
        const bool mutableView = previous().kind == TokenKind::SliceMutType;
        consume(TokenKind::LeftBracket, "'[' attendu après le type de slice");
        const ValueType element = consumeType("type d'élément attendu dans la slice");
        consume(TokenKind::RightBracket, "']' attendue après le type d'élément");
        return ValueType(ValueType::Kind::Slice,
                         std::make_shared<ValueType>(element), mutableView);
    }
    if (match(TokenKind::BoxType)) {
        consume(TokenKind::LeftBracket, "'[' attendu après 'Box'");
        const ValueType element = consumeType("type contenu attendu dans 'Box'");
        consume(TokenKind::RightBracket, "']' attendue après le type contenu");
        return ValueType(ValueType::Kind::Box, std::make_shared<ValueType>(element));
    }
    if (match(TokenKind::VecType)) {
        consume(TokenKind::LeftBracket, "'[' attendu après 'Vec'");
        const ValueType element = consumeType("type d'élément attendu dans 'Vec'");
        consume(TokenKind::RightBracket, "']' attendue après le type d'élément");
        return ValueType(ValueType::Kind::Vec, std::make_shared<ValueType>(element));
    }
    if (match(TokenKind::IntType)) return ValueType::Int;
    if (match(TokenKind::ByteType)) return ValueType::Byte;
    if (match(TokenKind::DoubleType)) return ValueType::Double;
    if (match(TokenKind::BoolType)) return ValueType::Bool;
    if (match(TokenKind::CharType)) return ValueType::Char;
    if (match(TokenKind::StringType)) return ValueType::String;
    if (match(TokenKind::StringViewType)) return ValueType::StringView;
    if (check(TokenKind::Identifier)) {
        const auto found = structures_.find(peek().text);
        if (found != structures_.end()) {
            const Token token = peek();
            ++current_;
            std::vector<ValueType> arguments;
            if (match(TokenKind::LeftBracket)) {
                if (!check(TokenKind::RightBracket)) {
                    do arguments.push_back(consumeType("argument de type attendu"));
                    while (match(TokenKind::Comma));
                }
                consume(TokenKind::RightBracket, "']' attendue après les arguments de type");
            }
            return ValueType(instantiateStructure(found->second, std::move(arguments), token.location));
        }
        const auto enumFound = enumerations_.find(peek().text);
        if (enumFound != enumerations_.end()) {
            const Token token = tokens_[current_++];
            std::vector<ValueType> arguments;
            if (match(TokenKind::LeftBracket)) {
                if (!check(TokenKind::RightBracket)) {
                    do arguments.push_back(consumeType("argument de type attendu"));
                    while (match(TokenKind::Comma));
                }
                consume(TokenKind::RightBracket, "']' attendue après les arguments de type");
            }
            return ValueType(instantiateEnumeration(
                enumFound->second, std::move(arguments), token.location));
        }
    }
    throw CompileError(peek().location, message + ", reçu " + tokenName(peek().kind));
}

std::shared_ptr<EnumType> Parser::enumeration() {
    const Token start = previous();
    const Token& name = consume(TokenKind::Identifier, "nom d'énumération attendu");
    if (structures_.contains(name.text) || enumerations_.contains(name.text))
        throw CompileError(name.location, "type '" + name.text + "' déclaré plusieurs fois");

    auto result = std::make_shared<EnumType>();
    result->location = start.location;
    result->name = name.text;
    enumerations_.emplace(name.text, result);

    activeTypeParameters_.clear();
    if (match(TokenKind::LeftBracket)) {
        do {
            const Token& parameter = consume(TokenKind::Identifier,
                                             "paramètre de type attendu");
            if (!activeTypeParameters_.insert(parameter.text).second)
                throw CompileError(parameter.location, "paramètre de type '" +
                    parameter.text + "' déclaré plusieurs fois");
            result->typeParameters.push_back(parameter.text);
        } while (match(TokenKind::Comma));
        consume(TokenKind::RightBracket, "']' attendue après les paramètres de type");
    }
    consume(TokenKind::LeftBrace, "'{' attendue après le nom d'énumération");
    skipSeparators();

    std::unordered_set<std::string> variantNames;
    std::size_t maximumPayloadSize = 0;
    std::size_t maximumPayloadAlignment = 1;
    while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
        const Token& variantName = consume(TokenKind::Identifier, "nom de variante attendu");
        if (!variantNames.insert(variantName.text).second)
            throw CompileError(variantName.location,
                               "variante '" + variantName.text + "' déclarée plusieurs fois");

        EnumVariant variant{variantName.location, variantName.text, {}};
        if (match(TokenKind::LeftParen)) {
            std::unordered_set<std::string> fieldNames;
            if (check(TokenKind::RightParen))
                throw CompileError(peek().location,
                                   "une variante vide ne doit pas utiliser de parenthèses");
            do {
                const Token& field = consume(TokenKind::Identifier, "nom de champ attendu");
                if (!fieldNames.insert(field.text).second)
                    throw CompileError(field.location,
                                       "champ '" + field.text + "' déclaré plusieurs fois dans la variante '" +
                                       variant.name + "'");
                consume(TokenKind::Colon, "':' attendu après le nom du champ");
                ValueType type = consumeType("type de champ attendu");
                if (type.kind == ValueType::Kind::Enum && type.enumeration.get() == result.get())
                    throw CompileError(field.location,
                                       "l'énumération '" + name.text +
                                       "' ne peut pas se contenir directement");
                const std::size_t alignment = type.kind == ValueType::Kind::TypeParameter
                    ? 1U : valueTypeAlignment(type);
                const std::size_t offset =
                    (variant.payloadSize + alignment - 1U) / alignment * alignment;
                variant.fields.push_back(StructField{field.location, field.text, type, offset});
                variant.payloadSize = offset +
                    (type.kind == ValueType::Kind::TypeParameter ? 0U : valueTypeSize(type));
                variant.payloadAlignment = std::max(variant.payloadAlignment, alignment);
            } while (match(TokenKind::Comma));
            consume(TokenKind::RightParen, "')' attendue après les champs de variante");
        }
        variant.payloadSize =
            (variant.payloadSize + variant.payloadAlignment - 1U) /
            variant.payloadAlignment * variant.payloadAlignment;
        maximumPayloadSize = std::max(maximumPayloadSize, variant.payloadSize);
        maximumPayloadAlignment = std::max(maximumPayloadAlignment, variant.payloadAlignment);
        result->variants.push_back(std::move(variant));

        if (!check(TokenKind::RightBrace) && !match(TokenKind::Comma) && !matchSeparator())
            throw CompileError(peek().location,
                               "fin de ligne, ',' ou '}' attendue après la variante");
        skipSeparators();
    }
    consume(TokenKind::RightBrace, "'}' attendue après les variantes");
    if (result->variants.empty())
        throw CompileError(name.location,
                           "l'énumération '" + name.text + "' doit contenir une variante");

    result->alignment = std::max<std::size_t>(4U, maximumPayloadAlignment);
    result->payloadOffset =
        (4U + maximumPayloadAlignment - 1U) / maximumPayloadAlignment * maximumPayloadAlignment;
    result->size = (result->payloadOffset + maximumPayloadSize + result->alignment - 1U) /
                   result->alignment * result->alignment;
    activeTypeParameters_.clear();
    return result;
}

std::shared_ptr<EnumType> Parser::instantiateEnumeration(
    const std::shared_ptr<EnumType>& enumeration, std::vector<ValueType> arguments,
    SourceLocation location) {
    if (arguments.size() != enumeration->typeParameters.size())
        throw CompileError(location, "l'énumération '" + enumeration->name + "' attend " +
            std::to_string(enumeration->typeParameters.size()) +
            " argument(s) de type, reçu " + std::to_string(arguments.size()));
    if (arguments.empty()) return enumeration;
    return std::const_pointer_cast<EnumType>(
        instantiateEnumType(enumeration, std::move(arguments), location));
}

std::shared_ptr<StructType> Parser::structure() {
    const Token start = previous();
    const Token& name = consume(TokenKind::Identifier, "nom de structure attendu");
    if (structures_.contains(name.text))
        throw CompileError(name.location, "structure '" + name.text + "' déclarée plusieurs fois");
    auto result = std::make_shared<StructType>();
    result->location = start.location;
    result->name = name.text;
    structures_.emplace(name.text, result);
    activeTypeParameters_.clear();
    if (match(TokenKind::LeftBracket)) {
        do {
            const Token& parameter = consume(TokenKind::Identifier, "paramètre de type attendu");
            if (!activeTypeParameters_.insert(parameter.text).second)
                throw CompileError(parameter.location, "paramètre de type '" + parameter.text + "' déclaré plusieurs fois");
            result->typeParameters.push_back(parameter.text);
        } while (match(TokenKind::Comma));
        consume(TokenKind::RightBracket, "']' attendue après les paramètres de type");
    }
    consume(TokenKind::LeftBrace, "'{' attendue après le nom de structure");
    skipSeparators();
    std::unordered_set<std::string> names;
    while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
        const Token& field = consume(TokenKind::Identifier, "nom de champ attendu");
        if (!names.insert(field.text).second)
            throw CompileError(field.location, "champ '" + field.text + "' déclaré plusieurs fois");
        consume(TokenKind::Colon, "':' attendu après le nom du champ");
        ValueType type = consumeType("type de champ attendu");
        if (type.kind == ValueType::Kind::Struct && type.structure.get() == result.get())
            throw CompileError(field.location, "la structure '" + name.text + "' ne peut pas se contenir directement");
        const std::size_t alignment = type.kind == ValueType::Kind::TypeParameter ? 1U : valueTypeAlignment(type);
        const std::size_t offset = (result->size + alignment - 1U) / alignment * alignment;
        result->fields.push_back(StructField{field.location, field.text, type, offset});
        result->size = offset + (type.kind == ValueType::Kind::TypeParameter ? 0U : valueTypeSize(type));
        result->alignment = std::max(result->alignment, alignment);
        if (!check(TokenKind::RightBrace) && !match(TokenKind::Comma) && !matchSeparator())
            throw CompileError(peek().location, "fin de ligne, ',' ou '}' attendue après le champ");
        skipSeparators();
    }
    consume(TokenKind::RightBrace, "'}' attendue après les champs");
    result->size = (result->size + result->alignment - 1U) / result->alignment * result->alignment;
    activeTypeParameters_.clear();
    return result;
}

std::shared_ptr<StructType> Parser::instantiateStructure(
    const std::shared_ptr<StructType>& structure, std::vector<ValueType> arguments,
    SourceLocation location) {
    if (arguments.size() != structure->typeParameters.size())
        throw CompileError(location, "la structure '" + structure->name + "' attend " +
            std::to_string(structure->typeParameters.size()) + " argument(s) de type, reçu " +
            std::to_string(arguments.size()));
    if (arguments.empty()) return structure;
    std::string key = structure->name;
    for (const ValueType& argument : arguments) key += "[" + typeName(argument) + "]";
    if (const auto cached = structureInstances_.find(key); cached != structureInstances_.end())
        return cached->second;
    auto instance = std::make_shared<StructType>();
    instance->location = location;
    instance->name = structure->name;
    instance->typeArguments = arguments;
    for (const StructField& field : structure->fields) {
        ValueType type = field.type;
        if (type.kind == ValueType::Kind::TypeParameter) {
            const auto parameter = std::find(structure->typeParameters.begin(), structure->typeParameters.end(), type.typeParameter);
            type = arguments[static_cast<std::size_t>(parameter - structure->typeParameters.begin())];
        }
        const std::size_t alignment = valueTypeAlignment(type);
        const std::size_t offset = (instance->size + alignment - 1U) / alignment * alignment;
        instance->fields.push_back(StructField{field.location, field.name, type, offset});
        instance->size = offset + valueTypeSize(type);
        instance->alignment = std::max(instance->alignment, alignment);
    }
    instance->size = (instance->size + instance->alignment - 1U) / instance->alignment * instance->alignment;
    structureInstances_.emplace(key, instance);
    return instance;
}

void Parser::skipSeparators() {
    while (matchSeparator()) {}
}

void Parser::expressionContinuation() {
    if (!checkSeparator()) return;
    if (blockDepth_ == 0) {
        throw CompileError(peek().location,
                           "une expression sur plusieurs lignes doit être entre accolades");
    }
    skipSeparators();
}

Program Parser::parse() {
    Program program;
    skipSeparators();
    while (match(TokenKind::Import)) {
        const Token start = previous();
        const Token& module = consume(TokenKind::Identifier,
                                      "nom de module attendu après 'import'");
        program.imports.push_back(Program::Import{start.location, module.text});
        importedModules_.insert(module.text);
        if (!check(TokenKind::End) && !matchSeparator())
            throw CompileError(peek().location, "fin d'instruction attendue après l'import");
        skipSeparators();
    }
    while (!check(TokenKind::End)) {
        if (match(TokenKind::Struct)) program.structures.push_back(structure());
        else if (match(TokenKind::Enum)) program.enumerations.push_back(enumeration());
        else program.statements.push_back(statement());
        if (!check(TokenKind::End) && !matchSeparator()) {
            throw CompileError(peek().location, "fin d'instruction attendue");
        }
        skipSeparators();
    }
    return program;
}

Statement Parser::statement() {
    if (match(TokenKind::Pub)) {
        if (blockDepth_ != 0)
            throw CompileError(previous().location,
                               "'pub' est autorisé uniquement au niveau global");
        publicDeclaration_ = true;
        if (!check(TokenKind::Native) && !check(TokenKind::Val) &&
            !check(TokenKind::Var) && !check(TokenKind::Def))
            throw CompileError(peek().location,
                               "'val', 'var' ou 'def' attendu après 'pub'");
    }
    if (match(TokenKind::Native)) {
        if (blockDepth_ != 0)
            throw CompileError(previous().location,
                               "'native' est autorisé uniquement au niveau global");
        nativeDeclaration_ = true;
        if (!check(TokenKind::Def))
            throw CompileError(peek().location, "'def' attendu après 'native'");
    }
    if (match(TokenKind::Val)) return declaration(BindingKind::Val);
    if (match(TokenKind::Var)) return declaration(BindingKind::Var);
    if (match(TokenKind::Def)) return declaration(BindingKind::Def);
    if (match(TokenKind::While)) return whileStatement();
    if (match(TokenKind::Return)) {
        const Token token = previous();
        return ReturnStatement{token.location, expression()};
    }
    if (match(TokenKind::Break)) return BreakStatement{previous().location};
    if (match(TokenKind::Continue)) return ContinueStatement{previous().location};
    if (check(TokenKind::Star)) return dereferenceAssignment();
    if (check(TokenKind::Identifier)) return assignment();
    throw CompileError(peek().location,
                       "instruction attendue ('val', 'var', 'def' ou affectation)");
}

Statement Parser::dereferenceAssignment() {
    const Token& star = consume(TokenKind::Star, "'*' attendu");
    const Token& name = consume(TokenKind::Identifier,
                                "référence attendue après '*'");
    consume(TokenKind::Equal, "'=' attendu après la référence déréférencée");
    auto reference = std::make_unique<Expression>(Expression{
        name.location, NameExpr{name.text}});
    return DereferenceAssignment{star.location, std::move(reference), expression()};
}

WhileStatement Parser::whileStatement() {
    const Token start = previous();
    consume(TokenKind::LeftParen, "'(' attendue après 'while'");
    expressionContinuation();
    ExprPtr condition = expression();
    expressionContinuation();
    consume(TokenKind::RightParen, "')' attendue après le prédicat");
    consume(TokenKind::Do, "'do' attendu après le prédicat de la boucle");
    consume(TokenKind::LeftBrace, "'{' attendue après 'do'");
    return WhileStatement{start.location, std::move(condition), loopBody()};
}

std::vector<StatementPtr> Parser::loopBody() {
    std::vector<StatementPtr> body;
    ++blockDepth_;
    skipSeparators();
    while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
        const bool startsStatement = check(TokenKind::Pub) || check(TokenKind::Native) ||
            check(TokenKind::Val) || check(TokenKind::Var) ||
            check(TokenKind::Def) || check(TokenKind::While) || check(TokenKind::Return) ||
            check(TokenKind::Break) || check(TokenKind::Continue) ||
            startsAssignment();
        const bool startsDereferenceAssignment = check(TokenKind::Star) &&
            current_ + 2 < tokens_.size() &&
            tokens_[current_ + 1].kind == TokenKind::Identifier &&
            tokens_[current_ + 2].kind == TokenKind::Equal;
        if (startsStatement || startsDereferenceAssignment) {
            body.push_back(std::make_unique<Statement>(statement()));
        } else {
            const SourceLocation location = peek().location;
            body.push_back(std::make_unique<Statement>(
                ExpressionStatement{location, expression()}));
        }
        if (!check(TokenKind::RightBrace) && !matchSeparator()) {
            throw CompileError(peek().location,
                               "fin de ligne ou ';' attendue dans le corps de la boucle");
        }
        skipSeparators();
    }
    consume(TokenKind::RightBrace, "'}' attendue à la fin de la boucle");
    --blockDepth_;
    return body;
}

Declaration Parser::declaration(BindingKind kind) {
    const Token start = previous();
    const Token& name = consume(TokenKind::Identifier,
                                "identifiant attendu après '" + start.text + "'");
    bool callable = false;
    std::vector<std::string> typeParameters;
    std::vector<std::string> typeConstraints;
    std::vector<Parameter> parameters;
    activeTypeParameters_.clear();
    if (kind == BindingKind::Def && match(TokenKind::LeftBracket)) {
        do {
            const Token& parameter = consume(TokenKind::Identifier,
                                             "nom de paramètre de type attendu");
            if (!activeTypeParameters_.insert(parameter.text).second)
                throw CompileError(parameter.location,
                                   "paramètre de type '" + parameter.text + "' déclaré plusieurs fois");
            typeParameters.push_back(parameter.text);
            std::string constraint;
            if (match(TokenKind::Colon))
                constraint = consume(TokenKind::Identifier,
                                     "nom de contrainte attendu après ':'").text;
            typeConstraints.push_back(std::move(constraint));
        } while (match(TokenKind::Comma));
        consume(TokenKind::RightBracket, "']' attendue après les paramètres de type");
    }
    if (kind == BindingKind::Def && match(TokenKind::LeftParen)) {
        callable = true;
        if (!check(TokenKind::RightParen)) {
            do {
                const Token& parameterName = consume(TokenKind::Identifier,
                                                     "nom de paramètre attendu");
                consume(TokenKind::Colon, "':' attendu après le paramètre");
                const ValueType parameterType = consumeType(
                    "type attendu pour le paramètre");
                parameters.push_back(Parameter{parameterName.location,
                                               parameterName.text,
                                               parameterType});
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RightParen, "')' attendue après les paramètres");
    }
    consume(TokenKind::Colon, "':' attendu après l'identifiant");
    const ValueType type = consumeType("type attendu");
    const bool publicSymbol = publicDeclaration_;
    const bool nativeSymbol = nativeDeclaration_;
    publicDeclaration_ = false;
    nativeDeclaration_ = false;
    if (nativeSymbol) {
        if (!callable)
            throw CompileError(start.location, "une déclaration native doit être une fonction");
        activeTypeParameters_.clear();
        return Declaration{start.location, name.text, type, kind, publicSymbol, true,
                           callable, std::move(parameters), std::move(typeParameters),
                           std::move(typeConstraints), nullptr};
    }
    consume(TokenKind::Equal, "'=' attendu après le type");
    ExprPtr initializer = expression();
    activeTypeParameters_.clear();
    return Declaration{start.location, name.text, type, kind, publicSymbol, false,
                       callable, std::move(parameters), std::move(typeParameters),
                       std::move(typeConstraints), std::move(initializer)};
}

std::string Parser::qualifiedName() {
    std::string name = previous().text;
    const bool moduleName = importedModules_.contains(name);
    std::size_t cursor = current_;
    while (cursor + 1 < tokens_.size() && tokens_[cursor].kind == TokenKind::Dot &&
           tokens_[cursor + 1].kind == TokenKind::Identifier) cursor += 2;
    if (!moduleName && (cursor >= tokens_.size() || tokens_[cursor].kind != TokenKind::LeftParen)) return name;
    while (match(TokenKind::Dot)) {
        const Token& part = consume(TokenKind::Identifier,
                                    "identifiant attendu après '.'");
        name += '.';
        name += part.text;
    }
    return name;
}

Statement Parser::assignment() {
    const Token& name = consume(TokenKind::Identifier, "identifiant attendu");
    if (match(TokenKind::Dot)) {
        const Token& field = consume(TokenKind::Identifier, "nom de champ attendu");
        consume(TokenKind::Equal, "'=' attendu après le champ");
        return FieldAssignment{name.location, name.text, field.text, expression()};
    }
    if (match(TokenKind::LeftBracket)) {
        std::vector<ExprPtr> indexes;
        do {
            indexes.push_back(expression());
            consume(TokenKind::RightBracket, "']' attendue après l'index");
        } while (match(TokenKind::LeftBracket));
        consume(TokenKind::Equal, "'=' attendu après la cible indexée");
        return IndexAssignment{name.location, name.text, std::move(indexes), expression()};
    }
    consume(TokenKind::Equal, "'=' attendu après l'identifiant");
    return Assignment{name.location, name.text, expression()};
}

ExprPtr Parser::expression() { return logicalOr(); }

ExprPtr Parser::logicalOr() {
    ExprPtr expr = logicalAnd();
    while (match(TokenKind::OrOr)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = logicalAnd();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::logicalAnd() {
    ExprPtr expr = equality();
    while (match(TokenKind::AndAnd)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = equality();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::equality() {
    ExprPtr expr = comparison();
    while (match(TokenKind::EqualEqual) || match(TokenKind::BangEqual)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = comparison();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::comparison() {
    ExprPtr expr = addition();
    while (match(TokenKind::Less) || match(TokenKind::LessEqual) ||
           match(TokenKind::Greater) || match(TokenKind::GreaterEqual)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = addition();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::addition() {
    ExprPtr expr = multiplication();
    while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = multiplication();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::multiplication() {
    ExprPtr expr = unary();
    while (match(TokenKind::Star) || match(TokenKind::Slash)) {
        const Token op = previous();
        expressionContinuation();
        ExprPtr right = unary();
        expr = std::make_unique<Expression>(Expression{
            op.location, BinaryExpr{op.text, std::move(expr), std::move(right)}});
    }
    return expr;
}

ExprPtr Parser::unary() {
    if (match(TokenKind::Ampersand)) {
        const Token token = previous();
        const bool mutableBorrow = match(TokenKind::Mut);
        return std::make_unique<Expression>(Expression{
            token.location, AddressExpr{mutableBorrow, unary()}});
    }
    if (match(TokenKind::Star)) {
        const Token token = previous();
        return std::make_unique<Expression>(Expression{
            token.location, DereferenceExpr{unary()}});
    }
    if (match(TokenKind::Minus) || match(TokenKind::Plus) || match(TokenKind::Bang)) {
        const Token op = previous();
        expressionContinuation();
        return std::make_unique<Expression>(Expression{
            op.location, UnaryExpr{op.text, unary()}});
    }
    return postfix();
}

ExprPtr Parser::postfix() {
    ExprPtr expr = primary();
    while (check(TokenKind::LeftBracket) || check(TokenKind::Dot)) {
        if (match(TokenKind::Dot)) {
            const Token& field = consume(TokenKind::Identifier, "nom de champ attendu après '.'");
            expr = std::make_unique<Expression>(Expression{
                field.location, FieldExpr{std::move(expr), field.text}});
            continue;
        }
        consume(TokenKind::LeftBracket, "'[' attendue");
        const Token token = previous();
        expressionContinuation();
        ExprPtr index = expression();
        expressionContinuation();
        consume(TokenKind::RightBracket, "']' attendue après l'index");
        expr = std::make_unique<Expression>(Expression{
            token.location, IndexExpr{std::move(expr), std::move(index)}});
    }
    return expr;
}

ExprPtr Parser::primary() {
    if (match(TokenKind::Match)) {
        return matchExpression(previous().location);
    }
    if (match(TokenKind::If)) {
        return ifExpression(previous().location);
    }
    if (match(TokenKind::LeftBrace)) {
        return blockExpression(previous().location);
    }
    if (match(TokenKind::LeftBracket)) {
        const Token token = previous();
        std::vector<ExprPtr> elements;
        if (!check(TokenKind::RightBracket)) {
            do {
                elements.push_back(expression());
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RightBracket, "']' attendue après le littéral de tableau");
        return std::make_unique<Expression>(Expression{
            token.location, ArrayExpr{std::move(elements)}});
    }
    if (match(TokenKind::SliceType) || match(TokenKind::SliceMutType)) {
        const Token token = previous();
        const bool mutableView = token.kind == TokenKind::SliceMutType;
        const ValueType target(ValueType::Kind::Slice,
                               std::make_shared<ValueType>(ValueType::Int), mutableView);
        consume(TokenKind::LeftParen, "'(' attendue après le type de slice");
        expressionContinuation();
        ExprPtr operand = expression();
        expressionContinuation();
        consume(TokenKind::RightParen, "')' attendue après le tableau emprunté");
        return std::make_unique<Expression>(Expression{
            token.location, ConversionExpr{target, std::move(operand)}});
    }
    if (match(TokenKind::BoxType)) {
        const Token token = previous();
        const ValueType target(ValueType::Kind::Box,
                               std::make_shared<ValueType>(ValueType::Int));
        consume(TokenKind::LeftParen, "'(' attendue après 'Box'");
        expressionContinuation();
        ExprPtr operand = expression();
        expressionContinuation();
        consume(TokenKind::RightParen, "')' attendue après la valeur à allouer");
        return std::make_unique<Expression>(Expression{
            token.location, ConversionExpr{target, std::move(operand)}});
    }
    if (match(TokenKind::VecType)) {
        const Token token = previous();
        consume(TokenKind::LeftBracket, "'[' attendue après 'Vec'");
        const ValueType element = consumeType("type d'élément attendu dans 'Vec'");
        consume(TokenKind::RightBracket, "']' attendue après le type d'élément");
        consume(TokenKind::LeftParen, "'(' attendue après le type de Vec");
        consume(TokenKind::RightParen, "')' attendue pour construire un Vec vide");
        return std::make_unique<Expression>(Expression{
            token.location, VecExpr{element}});
    }
    if (match(TokenKind::IntType) || match(TokenKind::ByteType) ||
        match(TokenKind::DoubleType) || match(TokenKind::BoolType) ||
        match(TokenKind::CharType) || match(TokenKind::StringType)) {
        const Token token = previous();
        const ValueType target = token.kind == TokenKind::IntType ? ValueType::Int :
            token.kind == TokenKind::ByteType ? ValueType::Byte :
            token.kind == TokenKind::DoubleType ? ValueType::Double :
            token.kind == TokenKind::BoolType ? ValueType::Bool :
            token.kind == TokenKind::CharType ? ValueType::Char : ValueType::String;
        consume(TokenKind::LeftParen, "'(' attendue après le type de conversion");
        expressionContinuation();
        ExprPtr operand = expression();
        expressionContinuation();
        consume(TokenKind::RightParen, "')' attendue après la valeur à convertir");
        return std::make_unique<Expression>(Expression{
            token.location, ConversionExpr{target, std::move(operand)}});
    }
    if (match(TokenKind::Integer)) {
        const Token token = previous();
        std::int64_t value{};
        const auto result = std::from_chars(token.text.data(), token.text.data() + token.text.size(), value);
        if (result.ec != std::errc{} || value > std::numeric_limits<std::int32_t>::max()) {
            throw CompileError(token.location, "entier hors de l'intervalle Int32");
        }
        return std::make_unique<Expression>(Expression{token.location, IntegerExpr{static_cast<std::int32_t>(value)}});
    }
    if (match(TokenKind::Character)) {
        const Token token = previous();
        return std::make_unique<Expression>(Expression{
            token.location, CharacterExpr{decodeCharacter(token)}});
    }
    if (match(TokenKind::String)) {
        const Token token = previous();
        return std::make_unique<Expression>(Expression{
            token.location, StringExpr{decodeString(token)}});
    }
    if (match(TokenKind::Floating)) {
        const Token token = previous();
        double value{};
        const auto result = std::from_chars(token.text.data(),
                                            token.text.data() + token.text.size(), value);
        if (result.ec != std::errc{} || result.ptr != token.text.data() + token.text.size() ||
            !std::isfinite(value)) {
            throw CompileError(token.location, "nombre Double invalide ou hors limites");
        }
        return std::make_unique<Expression>(Expression{token.location, DoubleExpr{value}});
    }
    if (match(TokenKind::True) || match(TokenKind::False)) {
        const Token token = previous();
        return std::make_unique<Expression>(Expression{
            token.location, BoolExpr{token.kind == TokenKind::True}});
    }
    if (match(TokenKind::Identifier)) {
        const Token token = previous();
        if (const auto definition = enumerations_.find(token.text);
            definition != enumerations_.end() &&
            (check(TokenKind::Dot) || check(TokenKind::LeftBracket))) {
            std::vector<ValueType> typeArguments;
            if (match(TokenKind::LeftBracket)) {
                if (!check(TokenKind::RightBracket)) {
                    do typeArguments.push_back(consumeType("argument de type attendu"));
                    while (match(TokenKind::Comma));
                }
                consume(TokenKind::RightBracket, "']' attendue après les arguments de type");
            }
            const auto enumeration = instantiateEnumeration(
                definition->second, std::move(typeArguments), token.location);
            consume(TokenKind::Dot, "'.' attendu après le nom d'énumération");
            const Token& variantToken =
                consume(TokenKind::Identifier, "nom de variante attendu après '.'");
            const auto& variants = enumeration->variants;
            const auto variant = std::find_if(variants.begin(), variants.end(),
                [&](const EnumVariant& candidate) { return candidate.name == variantToken.text; });
            if (variant == variants.end())
                throw CompileError(variantToken.location, "variante inconnue '" +
                    variantToken.text + "' pour " + token.text);

            std::vector<ExprPtr> fields(variant->fields.size());
            if (variant->fields.empty()) {
                if (check(TokenKind::LeftParen))
                    throw CompileError(peek().location,
                                       "la variante vide '" + variant->name +
                                       "' ne prend pas d'arguments");
            } else {
                consume(TokenKind::LeftParen,
                        "'(' attendue après la variante '" + variant->name + "'");
                std::unordered_set<std::string> initialized;
                do {
                    const Token& field = consume(TokenKind::Identifier,
                                                 "nom de champ de variante attendu");
                    const auto found = std::find_if(variant->fields.begin(),
                        variant->fields.end(), [&](const StructField& candidate) {
                            return candidate.name == field.text;
                        });
                    if (found == variant->fields.end())
                        throw CompileError(field.location, "champ inconnu '" + field.text +
                            "' pour " + token.text + "." + variant->name);
                    if (!initialized.insert(field.text).second)
                        throw CompileError(field.location, "champ '" + field.text +
                            "' initialisé plusieurs fois");
                    consume(TokenKind::Colon, "':' attendu après le nom du champ");
                    fields[static_cast<std::size_t>(found - variant->fields.begin())] = expression();
                } while (match(TokenKind::Comma));
                consume(TokenKind::RightParen, "')' attendue après les champs de variante");
                for (std::size_t i = 0; i < fields.size(); ++i)
                    if (!fields[i])
                        throw CompileError(variantToken.location, "champ '" +
                            variant->fields[i].name + "' manquant pour " + token.text +
                            "." + variant->name);
            }
            return std::make_unique<Expression>(Expression{token.location,
                EnumExpr{enumeration,
                    static_cast<std::size_t>(variant - variants.begin()), std::move(fields)}});
        }
        if (const auto definition = structures_.find(token.text);
            definition != structures_.end() &&
            (check(TokenKind::LeftBrace) || check(TokenKind::LeftBracket))) {
            std::vector<ValueType> arguments;
            if (match(TokenKind::LeftBracket)) {
                if (!check(TokenKind::RightBracket)) {
                    do arguments.push_back(consumeType("argument de type attendu"));
                    while (match(TokenKind::Comma));
                }
                consume(TokenKind::RightBracket, "']' attendue après les arguments de type");
            }
            auto structure = instantiateStructure(definition->second, std::move(arguments), token.location);
            consume(TokenKind::LeftBrace, "'{' attendue");
            skipSeparators();
            std::vector<ExprPtr> fields(structure->fields.size());
            std::unordered_set<std::string> initialized;
            while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
                const Token& field = consume(TokenKind::Identifier, "nom de champ attendu");
                const auto found = std::find_if(structure->fields.begin(),
                    structure->fields.end(), [&](const StructField& candidate) {
                        return candidate.name == field.text;
                    });
                if (found == structure->fields.end())
                    throw CompileError(field.location, "champ inconnu '" + field.text + "' pour " + token.text);
                if (!initialized.insert(field.text).second)
                    throw CompileError(field.location, "champ '" + field.text + "' initialisé plusieurs fois");
                consume(TokenKind::Colon, "':' attendu après le nom du champ");
                fields[static_cast<std::size_t>(found - structure->fields.begin())] = expression();
                if (!check(TokenKind::RightBrace) && !match(TokenKind::Comma) && !matchSeparator())
                    throw CompileError(peek().location, "fin de ligne, ',' ou '}' attendue");
                skipSeparators();
            }
            consume(TokenKind::RightBrace, "'}' attendue après les champs");
            for (std::size_t i = 0; i < fields.size(); ++i)
                if (!fields[i]) throw CompileError(token.location, "champ '" +
                    structure->fields[i].name + "' manquant pour " + token.text);
            return std::make_unique<Expression>(Expression{
                token.location, StructExpr{structure, std::move(fields)}});
        }
        const std::string name = qualifiedName();
        std::vector<ValueType> typeArguments;
        bool hasTypeArguments = false;
        if (check(TokenKind::LeftBracket)) {
            std::size_t cursor = current_;
            std::size_t depth = 0;
            do {
                if (tokens_[cursor].kind == TokenKind::LeftBracket) ++depth;
                else if (tokens_[cursor].kind == TokenKind::RightBracket) --depth;
                ++cursor;
            } while (cursor < tokens_.size() && depth != 0);
            hasTypeArguments = cursor < tokens_.size() &&
                tokens_[cursor].kind == TokenKind::LeftParen;
        }
        if (hasTypeArguments) {
            consume(TokenKind::LeftBracket, "'[' attendue avant les arguments de type");
            do {
                typeArguments.push_back(consumeType("argument de type attendu"));
            } while (match(TokenKind::Comma));
            consume(TokenKind::RightBracket, "']' attendue après les arguments de type");
        }
        if (match(TokenKind::LeftParen)) {
            std::vector<ExprPtr> arguments;
            expressionContinuation();
            if (!check(TokenKind::RightParen)) {
                do {
                    arguments.push_back(expression());
                    if (match(TokenKind::Comma)) {
                        expressionContinuation();
                    } else {
                        break;
                    }
                } while (true);
            }
            expressionContinuation();
            consume(TokenKind::RightParen, "')' attendue après les arguments");
            return std::make_unique<Expression>(Expression{
                token.location, CallExpr{name, std::move(typeArguments), std::move(arguments)}});
        }
        if (hasTypeArguments)
            throw CompileError(token.location,
                               "une instanciation générique doit être suivie d'un appel");
        return std::make_unique<Expression>(Expression{token.location, NameExpr{name}});
    }
    if (match(TokenKind::LeftParen)) {
        expressionContinuation();
        ExprPtr expr = expression();
        expressionContinuation();
        consume(TokenKind::RightParen, "')' attendue après l'expression");
        return expr;
    }
    throw CompileError(peek().location, "expression attendue");
}

ExprPtr Parser::matchExpression(SourceLocation location) {
    consume(TokenKind::LeftParen, "'(' attendue après 'match'");
    expressionContinuation();
    ExprPtr operand = expression();
    expressionContinuation();
    consume(TokenKind::RightParen, "')' attendue après l'expression de match");
    consume(TokenKind::LeftBrace, "'{' attendue après l'opérande de match");
    ++blockDepth_;
    skipSeparators();

    std::shared_ptr<const EnumType> matchedType;
    std::vector<MatchBranch> branches;
    std::unordered_set<std::size_t> covered;
    while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
        const Token& typeToken = consume(TokenKind::Identifier,
                                         "nom d'énumération attendu dans le motif");
        const auto definition = enumerations_.find(typeToken.text);
        if (definition == enumerations_.end())
            throw CompileError(typeToken.location,
                               "énumération inconnue '" + typeToken.text + "'");
        std::vector<ValueType> typeArguments;
        if (match(TokenKind::LeftBracket)) {
            if (!check(TokenKind::RightBracket)) {
                do typeArguments.push_back(consumeType("argument de type attendu"));
                while (match(TokenKind::Comma));
            }
            consume(TokenKind::RightBracket, "']' attendue après les arguments de type");
        }
        const auto patternType = instantiateEnumeration(
            definition->second, std::move(typeArguments), typeToken.location);
        if (matchedType == nullptr) matchedType = patternType;
        else if (matchedType.get() != patternType.get())
            throw CompileError(typeToken.location,
                               "tous les motifs doivent appartenir à " + matchedType->name);

        consume(TokenKind::Dot, "'.' attendu après le nom d'énumération");
        const Token& variantToken = consume(TokenKind::Identifier,
                                            "nom de variante attendu après '.'");
        const auto variant = std::find_if(matchedType->variants.begin(),
            matchedType->variants.end(), [&](const EnumVariant& candidate) {
                return candidate.name == variantToken.text;
            });
        if (variant == matchedType->variants.end())
            throw CompileError(variantToken.location, "variante inconnue '" +
                variantToken.text + "' pour " + matchedType->name);
        const std::size_t variantIndex =
            static_cast<std::size_t>(variant - matchedType->variants.begin());
        if (!covered.insert(variantIndex).second)
            throw CompileError(variantToken.location,
                               "variante '" + variant->name + "' couverte plusieurs fois");

        std::vector<std::optional<std::string>> bindings;
        std::unordered_set<std::string> bindingNames;
        if (variant->fields.empty()) {
            if (check(TokenKind::LeftParen))
                throw CompileError(peek().location,
                                   "la variante vide '" + variant->name +
                                   "' ne prend pas de liaisons");
        } else {
            consume(TokenKind::LeftParen,
                    "'(' attendue après la variante '" + variant->name + "'");
            if (!check(TokenKind::RightParen)) {
                do {
                    const Token& binding = consume(TokenKind::Identifier,
                                                   "nom de liaison ou '_' attendu");
                    if (binding.text == "_") bindings.push_back(std::nullopt);
                    else {
                        if (!bindingNames.insert(binding.text).second)
                            throw CompileError(binding.location, "liaison '" + binding.text +
                                               "' déclarée plusieurs fois dans le motif");
                        bindings.push_back(binding.text);
                    }
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RightParen, "')' attendue après les liaisons du motif");
            if (bindings.size() != variant->fields.size())
                throw CompileError(variantToken.location, "la variante '" + variant->name +
                    "' attend " + std::to_string(variant->fields.size()) +
                    " liaison(s), reçu " + std::to_string(bindings.size()));
        }
        consume(TokenKind::FatArrow, "'=>' attendu après le motif");
        expressionContinuation();
        ExprPtr result = expression();
        branches.push_back(MatchBranch{variantToken.location, variantIndex,
                                       std::move(bindings), std::move(result)});
        if (!check(TokenKind::RightBrace) && !match(TokenKind::Comma) && !matchSeparator())
            throw CompileError(peek().location,
                               "fin de ligne, ',' ou '}' attendue après la branche");
        skipSeparators();
    }
    consume(TokenKind::RightBrace, "'}' attendue après les branches de match");
    --blockDepth_;
    if (matchedType == nullptr)
        throw CompileError(location, "un match doit contenir au moins une branche");
    if (covered.size() != matchedType->variants.size()) {
        std::string missing;
        for (std::size_t i = 0; i < matchedType->variants.size(); ++i) {
            if (covered.contains(i)) continue;
            if (!missing.empty()) missing += ", ";
            missing += matchedType->name + "." + matchedType->variants[i].name;
        }
        throw CompileError(location, "correspondance incomplète, variante(s) manquante(s) : " +
                                     missing);
    }
    return std::make_unique<Expression>(Expression{
        location, MatchExpr{std::move(operand), matchedType, std::move(branches)}});
}

ExprPtr Parser::ifExpression(SourceLocation location) {
    consume(TokenKind::LeftParen, "'(' attendue après 'if'");
    expressionContinuation();
    ExprPtr condition = expression();
    expressionContinuation();
    consume(TokenKind::RightParen, "')' attendue après le prédicat");
    ExprPtr thenBranch = expression();
    skipSeparators();
    consume(TokenKind::Else, "'else' obligatoire après la première branche");
    ExprPtr elseBranch;
    if (match(TokenKind::If))
        elseBranch = ifExpression(previous().location);
    else
        elseBranch = expression();
    return std::make_unique<Expression>(Expression{
        location, IfExpr{std::move(condition), std::move(thenBranch),
                         std::move(elseBranch)}});
}

ExprPtr Parser::blockExpression(SourceLocation location) {
    ++blockDepth_;
    std::vector<StatementPtr> statements;
    skipSeparators();

    while (!check(TokenKind::RightBrace) && !check(TokenKind::End)) {
        const bool startsDeclaration = check(TokenKind::Val) || check(TokenKind::Var) ||
                                       check(TokenKind::Def) || check(TokenKind::While) ||
                                       check(TokenKind::Return) || check(TokenKind::Break) ||
                                       check(TokenKind::Continue);
        const bool startsDereferenceAssignment = check(TokenKind::Star) &&
            current_ + 2 < tokens_.size() &&
            tokens_[current_ + 1].kind == TokenKind::Identifier &&
            tokens_[current_ + 2].kind == TokenKind::Equal;
        if (!startsDeclaration && !startsAssignment() && !startsDereferenceAssignment) break;

        statements.push_back(std::make_unique<Statement>(statement()));
        if (!check(TokenKind::RightBrace) && !matchSeparator()) {
            throw CompileError(peek().location,
                               "fin de ligne ou ';' attendue après l'instruction du bloc");
        }
        skipSeparators();
    }

    if (check(TokenKind::RightBrace)) {
        if (!statements.empty() &&
            (std::holds_alternative<ReturnStatement>(statements.back()->value) ||
             std::holds_alternative<BreakStatement>(statements.back()->value) ||
             std::holds_alternative<ContinueStatement>(statements.back()->value))) {
            consume(TokenKind::RightBrace, "'}' attendue à la fin du bloc");
            --blockDepth_;
            return std::make_unique<Expression>(Expression{
                location, BlockExpr{std::move(statements), nullptr}});
        }
        throw CompileError(peek().location,
                           "un bloc doit se terminer par une expression");
    }
    ExprPtr result = expression();
    skipSeparators();
    consume(TokenKind::RightBrace, "'}' attendue à la fin du bloc");
    --blockDepth_;
    return std::make_unique<Expression>(Expression{
        location, BlockExpr{std::move(statements), std::move(result)}});
}

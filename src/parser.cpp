#include "parser.hpp"

#include "diagnostic.hpp"

#include <charconv>
#include <cmath>
#include <limits>

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

const Token& Parser::consume(TokenKind kind, const std::string& message) {
    if (check(kind)) return tokens_[current_++];
    throw CompileError(peek().location, message + ", reçu " + tokenName(peek().kind));
}

ValueType Parser::consumeType(const std::string& message) {
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
    if (match(TokenKind::IntType)) return ValueType::Int;
    if (match(TokenKind::ByteType)) return ValueType::Byte;
    if (match(TokenKind::DoubleType)) return ValueType::Double;
    if (match(TokenKind::BoolType)) return ValueType::Bool;
    if (match(TokenKind::CharType)) return ValueType::Char;
    if (match(TokenKind::StringType)) return ValueType::String;
    throw CompileError(peek().location, message + ", reçu " + tokenName(peek().kind));
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
        if (!check(TokenKind::End) && !matchSeparator())
            throw CompileError(peek().location, "fin d'instruction attendue après l'import");
        skipSeparators();
    }
    while (!check(TokenKind::End)) {
        program.statements.push_back(statement());
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
    if (check(TokenKind::Identifier)) return assignment();
    throw CompileError(peek().location,
                       "instruction attendue ('val', 'var', 'def' ou affectation)");
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
            (check(TokenKind::Identifier) && current_ + 1 < tokens_.size() &&
             tokens_[current_ + 1].kind == TokenKind::Equal);
        if (startsStatement) {
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
    std::vector<Parameter> parameters;
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
        return Declaration{start.location, name.text, type, kind, publicSymbol, true,
                           callable, std::move(parameters), nullptr};
    }
    consume(TokenKind::Equal, "'=' attendu après le type");
    return Declaration{start.location, name.text, type, kind, publicSymbol, false,
                       callable, std::move(parameters), expression()};
}

std::string Parser::qualifiedName() {
    std::string name = previous().text;
    while (match(TokenKind::Dot)) {
        const Token& part = consume(TokenKind::Identifier,
                                    "identifiant attendu après '.'");
        name += '.';
        name += part.text;
    }
    return name;
}

Assignment Parser::assignment() {
    const Token& name = consume(TokenKind::Identifier, "identifiant attendu");
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
    if (match(TokenKind::Minus) || match(TokenKind::Plus) || match(TokenKind::Bang)) {
        const Token op = previous();
        expressionContinuation();
        return std::make_unique<Expression>(Expression{
            op.location, UnaryExpr{op.text, unary()}});
    }
    return primary();
}

ExprPtr Parser::primary() {
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
        const std::string name = qualifiedName();
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
                token.location, CallExpr{name, std::move(arguments)}});
        }
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
        const bool startsAssignment = check(TokenKind::Identifier) &&
                                      current_ + 1 < tokens_.size() &&
                                      tokens_[current_ + 1].kind == TokenKind::Equal;
        if (!startsDeclaration && !startsAssignment) break;

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

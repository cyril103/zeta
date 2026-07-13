#include "interface.hpp"
#include "version.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {
using StructRegistry = std::unordered_map<std::string, std::shared_ptr<StructType>>;
using EnumRegistry = std::unordered_map<std::string, std::shared_ptr<EnumType>>;

std::string encodeType(const ValueType& type) {
    switch (type.kind) {
    case ValueType::Kind::Int: return "I";
    case ValueType::Kind::Byte: return "Y";
    case ValueType::Kind::Double: return "D";
    case ValueType::Kind::Bool: return "B";
    case ValueType::Kind::Char: return "C";
    case ValueType::Kind::String: return "S";
    case ValueType::Kind::StringView: return "W";
    case ValueType::Kind::Array:
        return "A" + std::to_string(type.length) + "(" + encodeType(*type.element) + ")";
    case ValueType::Kind::Reference:
        return std::string(type.mutableReference ? "RM(" : "R(") + encodeType(*type.element) + ")";
    case ValueType::Kind::Slice:
        return std::string(type.mutableReference ? "VM(" : "V(") + encodeType(*type.element) + ")";
    case ValueType::Kind::Box: return "O(" + encodeType(*type.element) + ")";
    case ValueType::Kind::Vec: return "G(" + encodeType(*type.element) + ")";
    case ValueType::Kind::TypeParameter: return "T(" + type.typeParameter + ")";
    case ValueType::Kind::Struct:
    {
        std::string encoded = "U" + std::to_string(type.structure->name.size()) + ":" +
            type.structure->name + "[" +
            std::to_string(type.structure->typeArguments.size()) + ":";
        for (const ValueType& argument : type.structure->typeArguments)
            encoded += encodeType(argument);
        return encoded + "]";
    }
    case ValueType::Kind::Enum:
    {
        std::string encoded = "E" + type.enumeration->name + "[" +
            std::to_string(type.enumeration->typeArguments.size()) + ":";
        for (const ValueType& argument : type.enumeration->typeArguments)
            encoded += encodeType(argument);
        return encoded + "]";
    }
    }
    throw std::runtime_error("type Zeta inconnu dans l'interface");
}

ValueType decodeType(const std::string& encoded, std::size_t& cursor,
                     const StructRegistry& structures,
                     const EnumRegistry& enumerations) {
    if (cursor >= encoded.size()) throw std::runtime_error("type tronqué dans l'interface");
    const char kind = encoded[cursor++];
    if (kind == 'I') return ValueType::Int;
    if (kind == 'Y') return ValueType::Byte;
    if (kind == 'D') return ValueType::Double;
    if (kind == 'B') return ValueType::Bool;
    if (kind == 'C') return ValueType::Char;
    if (kind == 'S') return ValueType::String;
    if (kind == 'W') return ValueType::StringView;
    if (kind == 'T') {
        if (cursor >= encoded.size() || encoded[cursor++] != '(')
            throw std::runtime_error("paramètre de type invalide dans l'interface");
        const std::size_t end = encoded.find(')', cursor);
        if (end == std::string::npos)
            throw std::runtime_error("paramètre de type non fermé dans l'interface");
        const std::string name = encoded.substr(cursor, end - cursor);
        cursor = end + 1U;
        return ValueType(ValueType::Kind::TypeParameter, name);
    }
    if (kind == 'U') {
        const std::size_t lengthBegin = cursor;
        while (cursor < encoded.size() &&
               std::isdigit(static_cast<unsigned char>(encoded[cursor]))) ++cursor;
        if (lengthBegin == cursor || cursor >= encoded.size() || encoded[cursor++] != ':')
            throw std::runtime_error("nom de structure invalide dans l'interface");
        const std::size_t nameLength = std::stoull(
            encoded.substr(lengthBegin, cursor - lengthBegin - 1U));
        if (cursor + nameLength > encoded.size())
            throw std::runtime_error("nom de structure tronqué dans l'interface");
        const std::string name = encoded.substr(cursor, nameLength);
        cursor += nameLength;
        if (cursor >= encoded.size() || encoded[cursor++] != '[')
            throw std::runtime_error("arguments de structure absents dans l'interface");
        const std::size_t countBegin = cursor;
        while (cursor < encoded.size() &&
               std::isdigit(static_cast<unsigned char>(encoded[cursor]))) ++cursor;
        if (countBegin == cursor || cursor >= encoded.size() || encoded[cursor++] != ':')
            throw std::runtime_error("arguments de structure invalides dans l'interface");
        const std::size_t count = std::stoull(
            encoded.substr(countBegin, cursor - countBegin - 1U));
        std::vector<ValueType> arguments;
        for (std::size_t i = 0; i < count; ++i)
            arguments.push_back(decodeType(encoded, cursor, structures, enumerations));
        if (cursor >= encoded.size() || encoded[cursor++] != ']')
            throw std::runtime_error("arguments de structure non fermés dans l'interface");
        const auto found = structures.find(name);
        if (found == structures.end())
            throw std::runtime_error("structure publique inconnue '" + name + "'");
        return ValueType(instantiateStructType(found->second, std::move(arguments), {}));
    }
    if (kind == 'E') {
        const std::size_t nameEnd = encoded.find('[', cursor);
        if (nameEnd == std::string::npos)
            throw std::runtime_error("nom d'énumération non fermé dans l'interface");
        const std::string name = encoded.substr(cursor, nameEnd - cursor);
        cursor = nameEnd + 1U;
        const std::size_t countEnd = encoded.find(':', cursor);
        if (countEnd == std::string::npos)
            throw std::runtime_error("arguments d'énumération invalides dans l'interface");
        const std::size_t count = std::stoull(encoded.substr(cursor, countEnd - cursor));
        cursor = countEnd + 1U;
        std::vector<ValueType> arguments;
        for (std::size_t i = 0; i < count; ++i)
            arguments.push_back(decodeType(encoded, cursor, structures, enumerations));
        if (cursor >= encoded.size() || encoded[cursor++] != ']')
            throw std::runtime_error("arguments d'énumération non fermés dans l'interface");
        if (const auto found = enumerations.find(name); found != enumerations.end())
            return ValueType(instantiateEnumType(
                found->second, std::move(arguments), {}));
        if (name != "Option")
            throw std::runtime_error("énumération publique inconnue '" + name + "'");
        auto option = std::make_shared<EnumType>();
        option->name = name;
        option->publicType = true;
        option->typeArguments = std::move(arguments);
        return ValueType(option);
    }
    bool mutableView = false;
    if ((kind == 'R' || kind == 'V') && cursor < encoded.size() && encoded[cursor] == 'M') {
        mutableView = true;
        ++cursor;
    }
    std::size_t length = 0;
    if (kind == 'A') {
        const std::size_t begin = cursor;
        while (cursor < encoded.size() && std::isdigit(static_cast<unsigned char>(encoded[cursor])))
            ++cursor;
        if (begin == cursor) throw std::runtime_error("taille de tableau absente dans l'interface");
        length = static_cast<std::size_t>(std::stoull(encoded.substr(begin, cursor - begin)));
    }
    if (cursor >= encoded.size() || encoded[cursor++] != '(')
        throw std::runtime_error("type composé invalide dans l'interface");
    ValueType element = decodeType(encoded, cursor, structures, enumerations);
    if (cursor >= encoded.size() || encoded[cursor++] != ')')
        throw std::runtime_error("type composé non fermé dans l'interface");
    if (kind == 'A') return ValueType(std::make_shared<ValueType>(element), length);
    if (kind == 'R') return ValueType(std::make_shared<ValueType>(element), mutableView);
    if (kind == 'V') return ValueType(ValueType::Kind::Slice,
        std::make_shared<ValueType>(element), mutableView);
    if (kind == 'O') return ValueType(ValueType::Kind::Box,
        std::make_shared<ValueType>(element));
    if (kind == 'G') return ValueType(ValueType::Kind::Vec,
        std::make_shared<ValueType>(element));
    throw std::runtime_error("code de type inconnu dans l'interface");
}

ValueType decodeType(const std::string& encoded, const StructRegistry& structures = {},
                     const EnumRegistry& enumerations = {}) {
    std::size_t cursor = 0;
    ValueType type = decodeType(encoded, cursor, structures, enumerations);
    if (cursor != encoded.size()) throw std::runtime_error("suffixe de type invalide dans l'interface");
    return type;
}

}

std::string InterfaceCodec::serialize(
    const ModuleInterface& interface, const std::string& fingerprint,
    const std::vector<std::string>& imports,
    const std::vector<Token>& genericTokens) {
    std::ostringstream output;
    output << "ZTI " << ZetaVersion::InterfaceFormat << "\nmodule "
           << std::quoted(interface.name) << '\n'
           << "fingerprint " << fingerprint << '\n';
    for (const std::string& import : imports)
        output << "import " << std::quoted(import) << '\n';
    if (!genericTokens.empty()) {
        output << "generic_tokens " << ZetaVersion::GenericTokens << ' '
               << genericTokens.size() << '\n';
        for (const Token& token : genericTokens)
            output << "token " << static_cast<int>(token.kind) << ' '
                   << token.location.line << ' ' << token.location.column << ' '
                   << std::quoted(token.text) << '\n';
    }
    for (const std::shared_ptr<const StructType>& structure : interface.structures) {
        output << "structure " << std::quoted(structure->name) << ' '
               << structure->size << ' ' << structure->alignment << ' '
               << structure->typeParameters.size() << ' ' << structure->fields.size();
        for (const std::string& parameter : structure->typeParameters)
            output << ' ' << std::quoted(parameter);
        output << '\n';
    }
    for (const std::shared_ptr<const EnumType>& enumeration : interface.enumerations) {
        output << "enumeration " << std::quoted(enumeration->name) << ' '
               << enumeration->size << ' ' << enumeration->alignment << ' '
               << enumeration->payloadOffset << ' '
               << enumeration->typeParameters.size() << ' '
               << enumeration->variants.size();
        for (const std::string& parameter : enumeration->typeParameters)
            output << ' ' << std::quoted(parameter);
        output << '\n';
    }
    for (const std::shared_ptr<const StructType>& structure : interface.structures) {
        output << "structure_body " << std::quoted(structure->name) << '\n';
        for (const StructField& field : structure->fields)
            output << "field " << std::quoted(field.name) << ' ' << field.offset << ' '
                   << std::quoted(encodeType(field.type)) << '\n';
    }
    for (const std::shared_ptr<const EnumType>& enumeration : interface.enumerations) {
        output << "enumeration_body " << std::quoted(enumeration->name) << '\n';
        for (const EnumVariant& variant : enumeration->variants) {
            output << "variant " << std::quoted(variant.name) << ' '
                   << variant.payloadSize << ' ' << variant.payloadAlignment << ' '
                   << variant.fields.size() << '\n';
            for (const StructField& field : variant.fields)
                output << "enum_field " << std::quoted(field.name) << ' '
                       << field.offset << ' ' << std::quoted(encodeType(field.type)) << '\n';
        }
    }
    std::vector<std::string> names;
    for (const auto& [name, symbol] : interface.exports) {
        static_cast<void>(symbol);
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        const ExportedSymbol& symbol = interface.exports.at(name);
        output << "export " << std::quoted(name) << ' '
               << static_cast<int>(symbol.kind) << ' '
               << (symbol.callable ? 1 : 0) << ' '
               << (symbol.nativeSymbol ? 1 : 0) << ' '
               << std::quoted(encodeType(symbol.type)) << ' '
               << symbol.parameterTypes.size();
        for (const ValueType& parameter : symbol.parameterTypes)
            output << ' ' << std::quoted(encodeType(parameter));
        const Declaration* declaration = symbol.declaration;
        const std::size_t genericCount = declaration == nullptr
            ? 0U : declaration->typeParameters.size();
        output << ' ' << genericCount;
        for (std::size_t i = 0; i < genericCount; ++i)
            output << ' ' << std::quoted(declaration->typeParameters[i])
                   << ' ' << std::quoted(declaration->typeConstraints[i]);
        output << '\n';
    }
    output << "end\n";
    return output.str();
}

PersistedInterface InterfaceCodec::deserialize(const std::string& contents) {
    std::istringstream input(contents);
    std::string word;
    int version = 0;
    if (!(input >> word >> version) || word != "ZTI" ||
        version != ZetaVersion::InterfaceFormat)
        throw std::runtime_error("format .zti inconnu ou incompatible");
    PersistedInterface result;
    StructRegistry structures;
    EnumRegistry enumerations;
    std::unordered_map<std::string, std::size_t> structureFieldCounts;
    std::unordered_map<std::string, std::size_t> enumerationVariantCounts;
    if (!(input >> word) || word != "module" || !(input >> std::quoted(result.interface.name)))
        throw std::runtime_error("nom de module absent de l'interface");
    if (!(input >> word) || word != "fingerprint" || !(input >> result.fingerprint))
        throw std::runtime_error("empreinte absente de l'interface");
    while (input >> word) {
        if (word == "end") break;
        if (word == "import") {
            std::string name;
            if (!(input >> std::quoted(name))) throw std::runtime_error("import .zti invalide");
            result.imports.push_back(std::move(name));
            continue;
        }
        if (word == "generic_tokens") {
            int tokenVersion = 0;
            std::size_t count = 0;
            if (!(input >> tokenVersion >> count) ||
                tokenVersion != ZetaVersion::GenericTokens || count == 0U ||
                !result.genericTokens.empty())
                throw std::runtime_error("représentation générique .zti invalide");
            for (std::size_t i = 0; i < count; ++i) {
                std::string tokenWord, text;
                int kind = 0;
                std::size_t line = 0, column = 0;
                if (!(input >> tokenWord >> kind >> line >> column >> std::quoted(text)) ||
                    tokenWord != "token" || kind < 0 ||
                    kind > static_cast<int>(TokenKind::End) || line == 0U || column == 0U)
                    throw std::runtime_error("token générique .zti invalide");
                const TokenKind tokenKind = static_cast<TokenKind>(kind);
                if (tokenKind == TokenKind::End && i + 1U != count)
                    throw std::runtime_error("fin prématurée des tokens génériques .zti");
                result.genericTokens.push_back(Token{
                    tokenKind, std::move(text), SourceLocation{line, column}});
            }
            if (result.genericTokens.back().kind != TokenKind::End)
                throw std::runtime_error("fin des tokens génériques .zti absente");
            continue;
        }
        if (word == "structure") {
            std::string name;
            std::size_t size = 0, alignment = 0, parameterCount = 0, fieldCount = 0;
            if (!(input >> std::quoted(name) >> size >> alignment >> parameterCount >> fieldCount) ||
                name.empty() || alignment == 0U)
                throw std::runtime_error("structure .zti invalide");
            if (structures.contains(name))
                throw std::runtime_error("structure .zti dupliquée : " + name);
            auto structure = std::make_shared<StructType>();
            structure->name = name;
            structure->publicType = true;
            structure->size = size;
            structure->alignment = alignment;
            for (std::size_t i = 0; i < parameterCount; ++i) {
                std::string parameter;
                if (!(input >> std::quoted(parameter)) || parameter.empty())
                    throw std::runtime_error("paramètre de structure .zti invalide");
                if (std::find(structure->typeParameters.begin(),
                              structure->typeParameters.end(), parameter) !=
                    structure->typeParameters.end())
                    throw std::runtime_error("paramètre de structure .zti dupliqué");
                structure->typeParameters.push_back(std::move(parameter));
            }
            structures.emplace(name, structure);
            structureFieldCounts.emplace(name, fieldCount);
            result.interface.structures.push_back(std::move(structure));
            continue;
        }
        if (word == "enumeration") {
            std::string name;
            std::size_t size = 0, alignment = 0, payloadOffset = 0;
            std::size_t parameterCount = 0, variantCount = 0;
            if (!(input >> std::quoted(name) >> size >> alignment >> payloadOffset >>
                  parameterCount >> variantCount) || name.empty() || alignment < 4U ||
                payloadOffset > size)
                throw std::runtime_error("énumération .zti invalide");
            if (enumerations.contains(name))
                throw std::runtime_error("énumération .zti dupliquée : " + name);
            auto enumeration = std::make_shared<EnumType>();
            enumeration->name = name;
            enumeration->publicType = true;
            enumeration->size = size;
            enumeration->alignment = alignment;
            enumeration->payloadOffset = payloadOffset;
            for (std::size_t i = 0; i < parameterCount; ++i) {
                std::string parameter;
                if (!(input >> std::quoted(parameter)) || parameter.empty())
                    throw std::runtime_error("paramètre d'énumération .zti invalide");
                if (std::find(enumeration->typeParameters.begin(),
                              enumeration->typeParameters.end(), parameter) !=
                    enumeration->typeParameters.end())
                    throw std::runtime_error("paramètre d'énumération .zti dupliqué");
                enumeration->typeParameters.push_back(std::move(parameter));
            }
            enumerations.emplace(name, enumeration);
            enumerationVariantCounts.emplace(name, variantCount);
            result.interface.enumerations.push_back(std::move(enumeration));
            continue;
        }
        if (word == "structure_body") {
            std::string name;
            if (!(input >> std::quoted(name)) || !structures.contains(name))
                throw std::runtime_error("corps de structure .zti invalide");
            const auto structure = structures.at(name);
            if (!structure->fields.empty())
                throw std::runtime_error("corps de structure .zti dupliqué");
            for (std::size_t i = 0; i < structureFieldCounts.at(name); ++i) {
                std::string fieldWord, fieldName, encodedType;
                std::size_t offset = 0;
                if (!(input >> fieldWord >> std::quoted(fieldName) >> offset >>
                      std::quoted(encodedType)) || fieldWord != "field" || fieldName.empty())
                    throw std::runtime_error("champ de structure .zti invalide");
                if (std::any_of(structure->fields.begin(), structure->fields.end(),
                    [&](const StructField& field) { return field.name == fieldName; }))
                    throw std::runtime_error("champ de structure .zti dupliqué");
                ValueType type = decodeType(encodedType, structures, enumerations);
                if (type.kind != ValueType::Kind::TypeParameter &&
                    offset + valueTypeSize(type) > structure->size)
                    throw std::runtime_error("champ hors disposition dans la structure .zti");
                structure->fields.push_back(StructField{{}, std::move(fieldName),
                                                        std::move(type), offset});
            }
            structureFieldCounts.erase(name);
            continue;
        }
        if (word == "enumeration_body") {
            std::string name;
            if (!(input >> std::quoted(name)) || !enumerations.contains(name))
                throw std::runtime_error("corps d'énumération .zti invalide");
            const auto enumeration = enumerations.at(name);
            if (!enumeration->variants.empty())
                throw std::runtime_error("corps d'énumération .zti dupliqué");
            for (std::size_t i = 0; i < enumerationVariantCounts.at(name); ++i) {
                std::string variantWord, variantName;
                std::size_t payloadSize = 0, payloadAlignment = 0, fieldCount = 0;
                if (!(input >> variantWord >> std::quoted(variantName) >> payloadSize >>
                      payloadAlignment >> fieldCount) || variantWord != "variant" ||
                    variantName.empty() || payloadAlignment == 0U)
                    throw std::runtime_error("variante d'énumération .zti invalide");
                if (std::any_of(enumeration->variants.begin(), enumeration->variants.end(),
                    [&](const EnumVariant& variant) { return variant.name == variantName; }))
                    throw std::runtime_error("variante d'énumération .zti dupliquée");
                EnumVariant variant{{}, std::move(variantName), {},
                                    payloadSize, payloadAlignment};
                for (std::size_t fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
                    std::string fieldWord, fieldName, encodedType;
                    std::size_t offset = 0;
                    if (!(input >> fieldWord >> std::quoted(fieldName) >> offset >>
                          std::quoted(encodedType)) || fieldWord != "enum_field" ||
                        fieldName.empty())
                        throw std::runtime_error("champ d'énumération .zti invalide");
                    if (std::any_of(variant.fields.begin(), variant.fields.end(),
                        [&](const StructField& field) { return field.name == fieldName; }))
                        throw std::runtime_error("champ d'énumération .zti dupliqué");
                    ValueType type = decodeType(encodedType, structures, enumerations);
                    if (type.kind != ValueType::Kind::TypeParameter &&
                        offset + valueTypeSize(type) > payloadSize)
                        throw std::runtime_error(
                            "champ hors charge dans l'énumération .zti");
                    variant.fields.push_back(StructField{
                        {}, std::move(fieldName), std::move(type), offset});
                }
                enumeration->variants.push_back(std::move(variant));
            }
            enumerationVariantCounts.erase(name);
            continue;
        }
        if (word != "export") throw std::runtime_error("entrée .zti inconnue : " + word);
        std::string name, returnType;
        int kind = 0, callable = 0, native = 0;
        std::size_t parameterCount = 0;
        if (!(input >> std::quoted(name) >> kind >> callable >> native >>
              std::quoted(returnType) >> parameterCount))
            throw std::runtime_error("export .zti invalide");
        std::vector<ValueType> parameters;
        for (std::size_t i = 0; i < parameterCount; ++i) {
            std::string encoded;
            if (!(input >> std::quoted(encoded))) throw std::runtime_error("paramètre .zti invalide");
            parameters.push_back(decodeType(encoded, structures, enumerations));
        }
        std::size_t genericCount = 0;
        if (!(input >> genericCount)) throw std::runtime_error("généricité .zti invalide");
        for (std::size_t i = 0; i < genericCount; ++i) {
            std::string ignoredName, ignoredConstraint;
            input >> std::quoted(ignoredName) >> std::quoted(ignoredConstraint);
        }
        result.interface.exports.emplace(name, ExportedSymbol{
            static_cast<BindingKind>(kind),
            decodeType(returnType, structures, enumerations), callable != 0,
            native != 0, std::move(parameters), nullptr});
    }
    if (!structureFieldCounts.empty() || !enumerationVariantCounts.empty())
        throw std::runtime_error("corps de type public absent de l'interface");
    return result;
}

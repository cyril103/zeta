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

[[noreturn]] void interfaceFailure(const std::string& code, const std::string& detail) {
    throw InterfaceError(code, detail);
}

std::string encodeType(const ValueType& type) {
    switch (type.kind) {
    case ValueType::Kind::Never:
        interfaceFailure("ZTI100", "le type interne Never ne peut pas être exporté");
    case ValueType::Kind::Unit: return "N";
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
    interfaceFailure("ZTI100", "type Zeta inconnu dans l'interface");
}

ValueType decodeType(const std::string& encoded, std::size_t& cursor,
                     const StructRegistry& structures,
                     const EnumRegistry& enumerations) {
    if (cursor >= encoded.size()) interfaceFailure("ZTI100", "type tronqué");
    const char kind = encoded[cursor++];
    if (kind == 'N') return ValueType::Unit;
    if (kind == 'I') return ValueType::Int;
    if (kind == 'Y') return ValueType::Byte;
    if (kind == 'D') return ValueType::Double;
    if (kind == 'B') return ValueType::Bool;
    if (kind == 'C') return ValueType::Char;
    if (kind == 'S') return ValueType::String;
    if (kind == 'W') return ValueType::StringView;
    if (kind == 'T') {
        if (cursor >= encoded.size() || encoded[cursor++] != '(')
            interfaceFailure("ZTI100", "paramètre de type invalide");
        const std::size_t end = encoded.find(')', cursor);
        if (end == std::string::npos)
            interfaceFailure("ZTI100", "paramètre de type non fermé");
        const std::string name = encoded.substr(cursor, end - cursor);
        cursor = end + 1U;
        return ValueType(ValueType::Kind::TypeParameter, name);
    }
    if (kind == 'U') {
        const std::size_t lengthBegin = cursor;
        while (cursor < encoded.size() &&
               std::isdigit(static_cast<unsigned char>(encoded[cursor]))) ++cursor;
        if (lengthBegin == cursor || cursor >= encoded.size() || encoded[cursor++] != ':')
            interfaceFailure("ZTI100", "nom de structure invalide");
        const std::size_t nameLength = std::stoull(
            encoded.substr(lengthBegin, cursor - lengthBegin - 1U));
        if (cursor + nameLength > encoded.size())
            interfaceFailure("ZTI100", "nom de structure tronqué");
        const std::string name = encoded.substr(cursor, nameLength);
        cursor += nameLength;
        if (cursor >= encoded.size() || encoded[cursor++] != '[')
            interfaceFailure("ZTI100", "arguments de structure absents");
        const std::size_t countBegin = cursor;
        while (cursor < encoded.size() &&
               std::isdigit(static_cast<unsigned char>(encoded[cursor]))) ++cursor;
        if (countBegin == cursor || cursor >= encoded.size() || encoded[cursor++] != ':')
            interfaceFailure("ZTI100", "arguments de structure invalides");
        const std::size_t count = std::stoull(
            encoded.substr(countBegin, cursor - countBegin - 1U));
        std::vector<ValueType> arguments;
        for (std::size_t i = 0; i < count; ++i)
            arguments.push_back(decodeType(encoded, cursor, structures, enumerations));
        if (cursor >= encoded.size() || encoded[cursor++] != ']')
            interfaceFailure("ZTI100", "arguments de structure non fermés");
        const auto found = structures.find(name);
        if (found == structures.end())
            interfaceFailure("ZTI100", "structure publique inconnue '" + name + "'");
        return ValueType(instantiateStructType(found->second, std::move(arguments), {}));
    }
    if (kind == 'E') {
        const std::size_t nameEnd = encoded.find('[', cursor);
        if (nameEnd == std::string::npos)
            interfaceFailure("ZTI100", "nom d'énumération non fermé");
        const std::string name = encoded.substr(cursor, nameEnd - cursor);
        cursor = nameEnd + 1U;
        const std::size_t countEnd = encoded.find(':', cursor);
        if (countEnd == std::string::npos)
            interfaceFailure("ZTI100", "arguments d'énumération invalides");
        const std::size_t count = std::stoull(encoded.substr(cursor, countEnd - cursor));
        cursor = countEnd + 1U;
        std::vector<ValueType> arguments;
        for (std::size_t i = 0; i < count; ++i)
            arguments.push_back(decodeType(encoded, cursor, structures, enumerations));
        if (cursor >= encoded.size() || encoded[cursor++] != ']')
            interfaceFailure("ZTI100", "arguments d'énumération non fermés");
        if (const auto found = enumerations.find(name); found != enumerations.end())
            return ValueType(instantiateEnumType(
                found->second, std::move(arguments), {}));
        if (name != "Option")
            interfaceFailure("ZTI100", "énumération publique inconnue '" + name + "'");
        return ValueType(instantiateEnumType(
            builtinOptionType(), std::move(arguments), {}));
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
        if (begin == cursor) interfaceFailure("ZTI100", "taille de tableau absente");
        length = static_cast<std::size_t>(std::stoull(encoded.substr(begin, cursor - begin)));
    }
    if (cursor >= encoded.size() || encoded[cursor++] != '(')
        interfaceFailure("ZTI100", "type composé invalide");
    ValueType element = decodeType(encoded, cursor, structures, enumerations);
    if (cursor >= encoded.size() || encoded[cursor++] != ')')
        interfaceFailure("ZTI100", "type composé non fermé");
    if (kind == 'A') return ValueType(std::make_shared<ValueType>(element), length);
    if (kind == 'R') return ValueType(std::make_shared<ValueType>(element), mutableView);
    if (kind == 'V') return ValueType(ValueType::Kind::Slice,
        std::make_shared<ValueType>(element), mutableView);
    if (kind == 'O') return ValueType(ValueType::Kind::Box,
        std::make_shared<ValueType>(element));
    if (kind == 'G') return ValueType(ValueType::Kind::Vec,
        std::make_shared<ValueType>(element));
    interfaceFailure("ZTI100", "code de type inconnu");
}

ValueType decodeType(const std::string& encoded, const StructRegistry& structures = {},
                     const EnumRegistry& enumerations = {}) {
    std::size_t cursor = 0;
    ValueType type = decodeType(encoded, cursor, structures, enumerations);
    if (cursor != encoded.size()) interfaceFailure("ZTI100", "suffixe de type invalide");
    return type;
}

ValueType decodeTypeContext(const std::string& encoded, const StructRegistry& structures,
                            const EnumRegistry& enumerations,
                            const std::string& context) {
    try {
        return decodeType(encoded, structures, enumerations);
    } catch (const InterfaceError& error) {
        throw InterfaceError(error.code(), context + " : " + error.detail());
    }
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
    std::vector<const TraitDeclaration*> traits;
    for (const TraitDeclaration& trait : interface.traits) traits.push_back(&trait);
    std::sort(traits.begin(), traits.end(),
        [](const TraitDeclaration* left, const TraitDeclaration* right) {
            return left->name < right->name;
        });
    for (const TraitDeclaration* trait : traits) {
        output << "trait " << std::quoted(trait->name) << ' ' << trait->methods.size() << '\n';
        for (const TraitMethodRequirement& method : trait->methods) {
            output << "trait_method " << std::quoted(method.name) << ' '
                   << std::quoted(encodeType(method.returnType)) << ' '
                   << method.parameters.size();
            for (const Parameter& parameter : method.parameters)
                output << ' ' << std::quoted(parameter.name) << ' '
                       << std::quoted(encodeType(parameter.type));
            output << '\n';
        }
    }
    std::vector<const TraitImplementation*> implementations;
    for (const TraitImplementation& implementation : interface.traitImplementations)
        implementations.push_back(&implementation);
    std::sort(implementations.begin(), implementations.end(),
        [](const TraitImplementation* left, const TraitImplementation* right) {
            if (left->trait != right->trait) return left->trait < right->trait;
            return typeName(left->type) < typeName(right->type);
        });
    for (const TraitImplementation* implementation : implementations) {
        output << "implementation " << std::quoted(implementation->trait) << ' '
               << std::quoted(encodeType(implementation->type)) << ' '
               << implementation->methods.size();
        for (const std::string& method : implementation->methods)
            output << ' ' << std::quoted(method);
        output << '\n';
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
               << (symbol.extensionMethod ? 1 : 0) << ' '
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
        interfaceFailure("ZTI001", "format .zti inconnu ou incompatible");
    PersistedInterface result;
    StructRegistry structures;
    EnumRegistry enumerations;
    std::unordered_map<std::string, std::size_t> structureFieldCounts;
    std::unordered_map<std::string, std::size_t> enumerationVariantCounts;
    if (!(input >> word) || word != "module" || !(input >> std::quoted(result.interface.name)))
        interfaceFailure("ZTI010", "nom de module absent de l'interface");
    if (!(input >> word) || word != "fingerprint" || !(input >> result.fingerprint))
        interfaceFailure("ZTI010", "empreinte absente de l'interface");
    while (input >> word) {
        if (word == "end") break;
        if (word == "import") {
            std::string name;
            if (!(input >> std::quoted(name)))
                interfaceFailure("ZTI010", "import .zti invalide");
            result.imports.push_back(std::move(name));
            continue;
        }
        if (word == "generic_tokens") {
            int tokenVersion = 0;
            std::size_t count = 0;
            if (!(input >> tokenVersion >> count) ||
                tokenVersion != ZetaVersion::GenericTokens || count == 0U ||
                !result.genericTokens.empty())
                interfaceFailure("ZTI300", "représentation générique .zti invalide");
            for (std::size_t i = 0; i < count; ++i) {
                std::string tokenWord, text;
                int kind = 0;
                std::size_t line = 0, column = 0;
                if (!(input >> tokenWord >> kind >> line >> column >> std::quoted(text)) ||
                    tokenWord != "token" || kind < 0 ||
                    kind > static_cast<int>(TokenKind::End) || line == 0U || column == 0U)
                    interfaceFailure("ZTI300", "token générique .zti invalide");
                const TokenKind tokenKind = static_cast<TokenKind>(kind);
                if (tokenKind == TokenKind::End && i + 1U != count)
                    interfaceFailure("ZTI300", "fin prématurée des tokens génériques .zti");
                result.genericTokens.push_back(Token{
                    tokenKind, std::move(text), SourceLocation{line, column}});
            }
            if (result.genericTokens.back().kind != TokenKind::End)
                interfaceFailure("ZTI300", "fin des tokens génériques .zti absente");
            continue;
        }
        if (word == "structure") {
            std::string name;
            std::size_t size = 0, alignment = 0, parameterCount = 0, fieldCount = 0;
            if (!(input >> std::quoted(name) >> size >> alignment >> parameterCount >> fieldCount) ||
                name.empty() || alignment == 0U)
                interfaceFailure("ZTI200", "disposition de structure invalide");
            if (structures.contains(name))
                interfaceFailure("ZTI010", "structure dupliquée '" + name + "'");
            auto structure = std::make_shared<StructType>();
            structure->name = name;
            structure->publicType = true;
            structure->size = size;
            structure->alignment = alignment;
            for (std::size_t i = 0; i < parameterCount; ++i) {
                std::string parameter;
                if (!(input >> std::quoted(parameter)) || parameter.empty())
                    interfaceFailure("ZTI010", "structure '" + name +
                        "' : paramètre de type invalide");
                if (std::find(structure->typeParameters.begin(),
                              structure->typeParameters.end(), parameter) !=
                    structure->typeParameters.end())
                    interfaceFailure("ZTI010", "structure '" + name +
                        "' : paramètre de type dupliqué '" + parameter + "'");
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
                interfaceFailure("ZTI200", "disposition d'énumération invalide");
            if (enumerations.contains(name))
                interfaceFailure("ZTI010", "énumération dupliquée '" + name + "'");
            auto enumeration = std::make_shared<EnumType>();
            enumeration->name = name;
            enumeration->publicType = true;
            enumeration->size = size;
            enumeration->alignment = alignment;
            enumeration->payloadOffset = payloadOffset;
            for (std::size_t i = 0; i < parameterCount; ++i) {
                std::string parameter;
                if (!(input >> std::quoted(parameter)) || parameter.empty())
                    interfaceFailure("ZTI010", "énumération '" + name +
                        "' : paramètre de type invalide");
                if (std::find(enumeration->typeParameters.begin(),
                              enumeration->typeParameters.end(), parameter) !=
                    enumeration->typeParameters.end())
                    interfaceFailure("ZTI010", "énumération '" + name +
                        "' : paramètre de type dupliqué '" + parameter + "'");
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
                interfaceFailure("ZTI010", "corps de structure inconnu ou invalide");
            const auto structure = structures.at(name);
            if (!structure->fields.empty())
                interfaceFailure("ZTI010", "corps de structure dupliqué '" + name + "'");
            for (std::size_t i = 0; i < structureFieldCounts.at(name); ++i) {
                std::string fieldWord, fieldName, encodedType;
                std::size_t offset = 0;
                if (!(input >> fieldWord >> std::quoted(fieldName) >> offset >>
                      std::quoted(encodedType)) || fieldWord != "field" || fieldName.empty())
                    interfaceFailure("ZTI010", "structure '" + name +
                        "' : entrée de champ invalide");
                if (std::any_of(structure->fields.begin(), structure->fields.end(),
                    [&](const StructField& field) { return field.name == fieldName; }))
                    interfaceFailure("ZTI200", "structure '" + name +
                        "' : champ dupliqué '" + fieldName + "'");
                ValueType type = decodeTypeContext(encodedType, structures, enumerations,
                    "structure '" + name + "', champ '" + fieldName + "'");
                if (type.kind != ValueType::Kind::TypeParameter &&
                    offset + valueTypeSize(type) > structure->size)
                    interfaceFailure("ZTI200", "structure '" + name + "', champ '" +
                        fieldName + "' : champ hors disposition");
                structure->fields.push_back(StructField{{}, std::move(fieldName),
                                                        std::move(type), offset});
            }
            structureFieldCounts.erase(name);
            continue;
        }
        if (word == "enumeration_body") {
            std::string name;
            if (!(input >> std::quoted(name)) || !enumerations.contains(name))
                interfaceFailure("ZTI010", "corps d'énumération inconnu ou invalide");
            const auto enumeration = enumerations.at(name);
            if (!enumeration->variants.empty())
                interfaceFailure("ZTI010", "corps d'énumération dupliqué '" + name + "'");
            for (std::size_t i = 0; i < enumerationVariantCounts.at(name); ++i) {
                std::string variantWord, variantName;
                std::size_t payloadSize = 0, payloadAlignment = 0, fieldCount = 0;
                if (!(input >> variantWord >> std::quoted(variantName) >> payloadSize >>
                      payloadAlignment >> fieldCount) || variantWord != "variant" ||
                    variantName.empty() || payloadAlignment == 0U)
                    interfaceFailure("ZTI010", "énumération '" + name +
                        "' : variante invalide");
                if (std::any_of(enumeration->variants.begin(), enumeration->variants.end(),
                    [&](const EnumVariant& variant) { return variant.name == variantName; }))
                    interfaceFailure("ZTI200", "énumération '" + name +
                        "' : variante dupliquée '" + variantName + "'");
                EnumVariant variant{{}, std::move(variantName), {},
                                    payloadSize, payloadAlignment};
                for (std::size_t fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
                    std::string fieldWord, fieldName, encodedType;
                    std::size_t offset = 0;
                    if (!(input >> fieldWord >> std::quoted(fieldName) >> offset >>
                          std::quoted(encodedType)) || fieldWord != "enum_field" ||
                        fieldName.empty())
                        interfaceFailure("ZTI010", "énumération '" + name +
                            "', variante '" + variant.name + "' : entrée de champ invalide");
                    if (std::any_of(variant.fields.begin(), variant.fields.end(),
                        [&](const StructField& field) { return field.name == fieldName; }))
                        interfaceFailure("ZTI200", "énumération '" + name +
                            "', variante '" + variant.name + "' : champ dupliqué '" +
                            fieldName + "'");
                    ValueType type = decodeTypeContext(encodedType, structures, enumerations,
                        "énumération '" + name + "', variante '" + variant.name +
                        "', champ '" + fieldName + "'");
                    if (type.kind != ValueType::Kind::TypeParameter &&
                        offset + valueTypeSize(type) > payloadSize)
                        interfaceFailure("ZTI200", "énumération '" + name +
                            "', variante '" + variant.name + "', champ '" + fieldName +
                            "' : champ hors charge");
                    variant.fields.push_back(StructField{
                        {}, std::move(fieldName), std::move(type), offset});
                }
                enumeration->variants.push_back(std::move(variant));
            }
            enumerationVariantCounts.erase(name);
            continue;
        }
        if (word == "trait") {
            std::string name;
            std::size_t methodCount = 0;
            if (!(input >> std::quoted(name) >> methodCount) || name.empty() ||
                std::any_of(result.interface.traits.begin(), result.interface.traits.end(),
                    [&](const TraitDeclaration& trait) { return trait.name == name; }))
                interfaceFailure("ZTI400", "déclaration de trait invalide ou dupliquée");
            TraitDeclaration trait{{}, std::move(name), true, {}};
            for (std::size_t methodIndex = 0; methodIndex < methodCount; ++methodIndex) {
                std::string methodWord, methodName, encodedReturn;
                std::size_t parameterCount = 0;
                if (!(input >> methodWord >> std::quoted(methodName) >>
                      std::quoted(encodedReturn) >> parameterCount) ||
                    methodWord != "trait_method" || methodName.empty() ||
                    std::any_of(trait.methods.begin(), trait.methods.end(),
                        [&](const TraitMethodRequirement& method) {
                            return method.name == methodName;
                        }))
                    interfaceFailure("ZTI400", "signature de méthode de trait invalide");
                std::vector<Parameter> parameters;
                for (std::size_t i = 0; i < parameterCount; ++i) {
                    std::string parameterName, encodedType;
                    if (!(input >> std::quoted(parameterName) >> std::quoted(encodedType)) ||
                        parameterName.empty())
                        interfaceFailure("ZTI400", "paramètre de méthode de trait invalide");
                    parameters.push_back(Parameter{{}, std::move(parameterName),
                        decodeTypeContext(encodedType, structures, enumerations,
                            "méthode de trait '" + methodName + "'")});
                }
                trait.methods.push_back(TraitMethodRequirement{{}, std::move(methodName),
                    std::move(parameters),
                    decodeTypeContext(encodedReturn, structures, enumerations,
                        "retour de méthode du trait '" + trait.name + "'")});
            }
            result.interface.traits.push_back(std::move(trait));
            continue;
        }
        if (word == "implementation") {
            std::string trait, encodedType;
            std::size_t methodCount = 0;
            if (!(input >> std::quoted(trait) >> std::quoted(encodedType) >> methodCount) ||
                trait.empty())
                interfaceFailure("ZTI400", "implémentation de trait invalide");
            ValueType type = decodeTypeContext(encodedType, structures, enumerations,
                                               "implémentation du trait '" + trait + "'");
            const bool duplicate = std::any_of(result.interface.traitImplementations.begin(),
                result.interface.traitImplementations.end(),
                [&](const TraitImplementation& implementation) {
                    return implementation.trait == trait && implementation.type == type;
                });
            if (duplicate)
                interfaceFailure("ZTI400", "implémentation dupliquée du trait '" + trait +
                                           "' pour " + typeName(type));
            std::vector<std::string> methods;
            for (std::size_t i = 0; i < methodCount; ++i) {
                std::string method;
                if (!(input >> std::quoted(method)) || method.empty() ||
                    std::find(methods.begin(), methods.end(), method) != methods.end())
                    interfaceFailure("ZTI400", "méthode d'implémentation invalide");
                methods.push_back(std::move(method));
            }
            result.interface.traitImplementations.push_back(TraitImplementation{
                {}, std::move(trait), std::move(type), std::move(methods)});
            continue;
        }
        if (word != "export")
            interfaceFailure("ZTI010", "entrée inconnue '" + word + "'");
        std::string name, returnType;
        int kind = 0, callable = 0, native = 0, extension = 0;
        std::size_t parameterCount = 0;
        if (!(input >> std::quoted(name) >> kind >> callable >> native >> extension >>
              std::quoted(returnType) >> parameterCount))
            interfaceFailure("ZTI010", "entrée d'export invalide");
        std::vector<ValueType> parameters;
        for (std::size_t i = 0; i < parameterCount; ++i) {
            std::string encoded;
            if (!(input >> std::quoted(encoded)))
                interfaceFailure("ZTI010", "export '" + name +
                    "' : paramètre absent ou invalide");
            parameters.push_back(decodeTypeContext(encoded, structures, enumerations,
                "export '" + name + "', paramètre " + std::to_string(i + 1U)));
        }
        std::size_t genericCount = 0;
        if (!(input >> genericCount))
            interfaceFailure("ZTI010", "export '" + name +
                "' : métadonnées génériques invalides");
        for (std::size_t i = 0; i < genericCount; ++i) {
            std::string ignoredName, ignoredConstraint;
            if (!(input >> std::quoted(ignoredName) >> std::quoted(ignoredConstraint)))
                interfaceFailure("ZTI010", "export '" + name +
                    "' : paramètre générique invalide");
        }
        if (!result.interface.exports.emplace(name, ExportedSymbol{
            static_cast<BindingKind>(kind),
            decodeTypeContext(returnType, structures, enumerations,
                              "export '" + name + "', type de retour"), callable != 0,
            native != 0, std::move(parameters), nullptr, extension != 0}).second)
            interfaceFailure("ZTI010", "export dupliqué '" + name + "'");
    }
    if (!structureFieldCounts.empty() || !enumerationVariantCounts.empty())
        interfaceFailure("ZTI200", "corps de type public absent de l'interface");
    return result;
}

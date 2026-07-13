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
                     const StructRegistry& structures) {
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
            arguments.push_back(decodeType(encoded, cursor, structures));
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
        auto enumeration = std::make_shared<EnumType>();
        enumeration->name = encoded.substr(cursor, nameEnd - cursor);
        cursor = nameEnd + 1U;
        const std::size_t countEnd = encoded.find(':', cursor);
        if (countEnd == std::string::npos)
            throw std::runtime_error("arguments d'énumération invalides dans l'interface");
        const std::size_t count = std::stoull(encoded.substr(cursor, countEnd - cursor));
        cursor = countEnd + 1U;
        for (std::size_t i = 0; i < count; ++i)
            enumeration->typeArguments.push_back(decodeType(encoded, cursor, structures));
        if (cursor >= encoded.size() || encoded[cursor++] != ']')
            throw std::runtime_error("arguments d'énumération non fermés dans l'interface");
        return ValueType(enumeration);
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
    ValueType element = decodeType(encoded, cursor, structures);
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

ValueType decodeType(const std::string& encoded, const StructRegistry& structures = {}) {
    std::size_t cursor = 0;
    ValueType type = decodeType(encoded, cursor, structures);
    if (cursor != encoded.size()) throw std::runtime_error("suffixe de type invalide dans l'interface");
    return type;
}

std::string hexEncode(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char byte : value) {
        encoded.push_back(digits[byte >> 4U]);
        encoded.push_back(digits[byte & 0x0FU]);
    }
    return encoded;
}

std::string hexDecode(const std::string& encoded) {
    if (encoded.size() % 2U != 0) throw std::runtime_error("source générique .zti invalide");
    const auto nibble = [](char value) -> unsigned {
        if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<unsigned>(value - 'a' + 10);
        throw std::runtime_error("source générique .zti invalide");
    };
    std::string decoded;
    decoded.reserve(encoded.size() / 2U);
    for (std::size_t i = 0; i < encoded.size(); i += 2U)
        decoded.push_back(static_cast<char>((nibble(encoded[i]) << 4U) | nibble(encoded[i + 1U])));
    return decoded;
}
}

std::string InterfaceCodec::serialize(
    const ModuleInterface& interface, const std::string& fingerprint,
    const std::vector<std::string>& imports, const std::string& genericSource) {
    std::ostringstream output;
    output << "ZTI " << ZetaVersion::InterfaceFormat << "\nmodule "
           << std::quoted(interface.name) << '\n'
           << "fingerprint " << fingerprint << '\n';
    for (const std::string& import : imports)
        output << "import " << std::quoted(import) << '\n';
    if (!genericSource.empty()) output << "generic_source " << hexEncode(genericSource) << '\n';
    for (const std::shared_ptr<const StructType>& structure : interface.structures) {
        output << "structure " << std::quoted(structure->name) << ' '
               << structure->size << ' ' << structure->alignment << ' '
               << structure->typeParameters.size() << ' ' << structure->fields.size();
        for (const std::string& parameter : structure->typeParameters)
            output << ' ' << std::quoted(parameter);
        output << '\n';
        for (const StructField& field : structure->fields)
            output << "field " << std::quoted(field.name) << ' ' << field.offset << ' '
                   << std::quoted(encodeType(field.type)) << '\n';
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
        if (word == "generic_source") {
            std::string encoded;
            if (!(input >> encoded)) throw std::runtime_error("source générique .zti absente");
            result.genericSource = hexDecode(encoded);
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
            for (std::size_t i = 0; i < fieldCount; ++i) {
                std::string fieldWord, fieldName, encodedType;
                std::size_t offset = 0;
                if (!(input >> fieldWord >> std::quoted(fieldName) >> offset >>
                      std::quoted(encodedType)) || fieldWord != "field" || fieldName.empty())
                    throw std::runtime_error("champ de structure .zti invalide");
                if (std::any_of(structure->fields.begin(), structure->fields.end(),
                    [&](const StructField& field) { return field.name == fieldName; }))
                    throw std::runtime_error("champ de structure .zti dupliqué");
                ValueType type = decodeType(encodedType, structures);
                if (type.kind != ValueType::Kind::TypeParameter &&
                    offset + valueTypeSize(type) > size)
                    throw std::runtime_error("champ hors disposition dans la structure .zti");
                structure->fields.push_back(StructField{{}, std::move(fieldName),
                                                        std::move(type), offset});
            }
            result.interface.structures.push_back(std::move(structure));
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
            parameters.push_back(decodeType(encoded, structures));
        }
        std::size_t genericCount = 0;
        if (!(input >> genericCount)) throw std::runtime_error("généricité .zti invalide");
        for (std::size_t i = 0; i < genericCount; ++i) {
            std::string ignoredName, ignoredConstraint;
            input >> std::quoted(ignoredName) >> std::quoted(ignoredConstraint);
        }
        result.interface.exports.emplace(name, ExportedSymbol{
            static_cast<BindingKind>(kind), decodeType(returnType, structures), callable != 0,
            native != 0, std::move(parameters), nullptr});
    }
    return result;
}

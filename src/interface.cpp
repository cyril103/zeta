#include "interface.hpp"
#include "version.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
std::string encodeType(const ValueType& type) {
    switch (type.kind) {
    case ValueType::Kind::Int: return "I";
    case ValueType::Kind::Byte: return "Y";
    case ValueType::Kind::Double: return "D";
    case ValueType::Kind::Bool: return "B";
    case ValueType::Kind::Char: return "C";
    case ValueType::Kind::String: return "S";
    case ValueType::Kind::Array:
        return "A" + std::to_string(type.length) + "(" + encodeType(*type.element) + ")";
    case ValueType::Kind::Reference:
        return std::string(type.mutableReference ? "RM(" : "R(") + encodeType(*type.element) + ")";
    case ValueType::Kind::Slice:
        return std::string(type.mutableReference ? "VM(" : "V(") + encodeType(*type.element) + ")";
    case ValueType::Kind::Box: return "O(" + encodeType(*type.element) + ")";
    case ValueType::Kind::TypeParameter: return "T(" + type.typeParameter + ")";
    case ValueType::Kind::Struct:
        throw std::runtime_error("les structures publiques ne sont pas encore sérialisables");
    }
    throw std::runtime_error("type Zeta inconnu dans l'interface");
}

ValueType decodeType(const std::string& encoded, std::size_t& cursor) {
    if (cursor >= encoded.size()) throw std::runtime_error("type tronqué dans l'interface");
    const char kind = encoded[cursor++];
    if (kind == 'I') return ValueType::Int;
    if (kind == 'Y') return ValueType::Byte;
    if (kind == 'D') return ValueType::Double;
    if (kind == 'B') return ValueType::Bool;
    if (kind == 'C') return ValueType::Char;
    if (kind == 'S') return ValueType::String;
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
    ValueType element = decodeType(encoded, cursor);
    if (cursor >= encoded.size() || encoded[cursor++] != ')')
        throw std::runtime_error("type composé non fermé dans l'interface");
    if (kind == 'A') return ValueType(std::make_shared<ValueType>(element), length);
    if (kind == 'R') return ValueType(std::make_shared<ValueType>(element), mutableView);
    if (kind == 'V') return ValueType(ValueType::Kind::Slice,
        std::make_shared<ValueType>(element), mutableView);
    if (kind == 'O') return ValueType(ValueType::Kind::Box,
        std::make_shared<ValueType>(element));
    throw std::runtime_error("code de type inconnu dans l'interface");
}

ValueType decodeType(const std::string& encoded) {
    std::size_t cursor = 0;
    ValueType type = decodeType(encoded, cursor);
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
            parameters.push_back(decodeType(encoded));
        }
        std::size_t genericCount = 0;
        if (!(input >> genericCount)) throw std::runtime_error("généricité .zti invalide");
        for (std::size_t i = 0; i < genericCount; ++i) {
            std::string ignoredName, ignoredConstraint;
            input >> std::quoted(ignoredName) >> std::quoted(ignoredConstraint);
        }
        result.interface.exports.emplace(name, ExportedSymbol{
            static_cast<BindingKind>(kind), decodeType(returnType), callable != 0,
            native != 0, std::move(parameters), nullptr});
    }
    return result;
}

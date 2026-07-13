#include "module.hpp"

#include "lexer.hpp"
#include "parser.hpp"
#include "interface.hpp"
#include "generic_tokens.hpp"
#include "version.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
std::string readSource(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("[MOD001] fichier introuvable : " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void validatePrecompiledObjectFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 20> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()),
                    static_cast<std::streamsize>(header.size())))
        throw std::runtime_error("[ABI001] " + path.string() +
                                 " : objet trop court pour un en-tête ELF");
    if (header[0] != 0x7FU || header[1] != 'E' || header[2] != 'L' || header[3] != 'F')
        throw std::runtime_error("[ABI001] " + path.string() +
                                 " : magie ELF invalide");
    const bool elf64 = header[4] == 2U;
    const bool littleEndian = header[5] == 1U;
    const bool currentVersion = header[6] == 1U;
    const std::uint16_t objectType =
        static_cast<std::uint16_t>(header[16]) |
        (static_cast<std::uint16_t>(header[17]) << 8U);
    const std::uint16_t machine =
        static_cast<std::uint16_t>(header[18]) |
        (static_cast<std::uint16_t>(header[19]) << 8U);
    if (!elf64 || !littleEndian || !currentVersion || objectType != 1U || machine != 62U)
        throw std::runtime_error("[ABI002] " + path.string() +
            " : objet incompatible, ELF64 little-endian relogeable x86-64 attendu");
}

bool validModuleName(const std::string& name) {
    if (name.empty() || (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_'))
        return false;
    for (char character : name)
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_') return false;
    return true;
}

std::uint64_t hashText(std::string_view text, std::uint64_t hash = 1469598103934665603ULL) {
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hexHash(std::uint64_t hash) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

std::uint64_t hashTokens(const std::vector<Token>& tokens) {
    std::uint64_t hash = 1469598103934665603ULL;
    bool hasToken = false;
    bool pendingSeparator = false;
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Separator) {
            pendingSeparator = hasToken;
            continue;
        }
        if (pendingSeparator && token.kind != TokenKind::End)
            hash = hashText("separator", hash);
        pendingSeparator = false;
        hash = hashText(std::to_string(static_cast<int>(token.kind)) + ":" +
                        std::to_string(token.text.size()) + ":" + token.text, hash);
        hasToken = true;
    }
    return hash;
}

std::vector<Program::Import> discoverImports(const std::vector<Token>& tokens) {
    std::vector<Program::Import> imports;
    std::size_t cursor = 0;
    const auto skipSeparators = [&] {
        while (cursor < tokens.size() &&
               (tokens[cursor].kind == TokenKind::Separator ||
                tokens[cursor].kind == TokenKind::Semicolon)) ++cursor;
    };
    skipSeparators();
    while (cursor < tokens.size() && tokens[cursor].kind == TokenKind::Import) {
        const SourceLocation location = tokens[cursor++].location;
        if (cursor >= tokens.size() || tokens[cursor].kind != TokenKind::Identifier) break;
        imports.push_back(Program::Import{location, tokens[cursor++].text});
        skipSeparators();
    }
    return imports;
}

Parser::ImportedStructures importedStructures(
    const std::vector<Program::Import>& imports,
    const std::unordered_map<std::string, ModuleInterface>& interfaces) {
    Parser::ImportedStructures result;
    for (const Program::Import& import : imports) {
        const auto module = interfaces.find(import.module);
        if (module == interfaces.end()) continue;
        for (const std::shared_ptr<const StructType>& structure : module->second.structures)
            result.emplace(import.module + "." + structure->name, structure);
    }
    return result;
}

Parser::ImportedEnumerations importedEnumerations(
    const std::vector<Program::Import>& imports,
    const std::unordered_map<std::string, ModuleInterface>& interfaces) {
    Parser::ImportedEnumerations result;
    for (const Program::Import& import : imports) {
        const auto module = interfaces.find(import.module);
        if (module == interfaces.end()) continue;
        for (const std::shared_ptr<const EnumType>& enumeration : module->second.enumerations)
            result.emplace(import.module + "." + enumeration->name, enumeration);
    }
    return result;
}
}

void validatePrecompiledObject(const std::filesystem::path& path) {
    validatePrecompiledObjectFile(path);
}

ModuleGraph ModuleLoader::load(const std::filesystem::path& rootPath) {
    graph_ = ModuleGraph{};
    loading_.clear();
    const std::filesystem::path absolute = std::filesystem::absolute(rootPath);
    sourceDirectory_ = absolute.parent_path();
#ifdef ZETA_STDLIB_DIR
    if (standardLibraryDirectory_.empty()) standardLibraryDirectory_ = ZETA_STDLIB_DIR;
#endif
    graph_.root = absolute.stem().string();
    if (!validModuleName(graph_.root))
        throw std::runtime_error("nom de module invalide : " + graph_.root);
    loadModule(graph_.root, absolute);
    buildInterfaces();
    buildDependencyGraph();
    buildFingerprints();
    return std::move(graph_);
}

void ModuleLoader::buildDependencyGraph() {
    for (const auto& [name, module] : graph_.modules) {
        auto& dependencies = graph_.dependencies[name];
        for (const Program::Import& import : module.program.imports)
            dependencies.push_back(import.module);
    }

    enum class State { Unvisited, Visiting, Visited };
    std::unordered_map<std::string, State> states;
    std::vector<std::string> path;
    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (states[name] == State::Visited) return;
        if (states[name] == State::Visiting) {
            const auto begin = std::find(path.begin(), path.end(), name);
            std::string message = "cycle d'import détecté : ";
            for (auto current = begin; current != path.end(); ++current) {
                if (current != begin) message += " -> ";
                message += *current;
            }
            message += " -> " + name;
            throw std::runtime_error(message);
        }
        states[name] = State::Visiting;
        path.push_back(name);
        for (const std::string& dependency : graph_.dependencies[name]) visit(dependency);
        path.pop_back();
        states[name] = State::Visited;
        graph_.compilationOrder.push_back(name);
    };
    visit(graph_.root);
}

void ModuleLoader::loadModule(const std::string& name, const std::filesystem::path& path) {
    if (graph_.modules.contains(name)) return;
    if (!validModuleName(name)) throw std::runtime_error("nom de module invalide : " + name);
    if (!loading_.insert(name).second)
        throw std::runtime_error("cycle d'import détecté autour du module " + name);

    const std::string source = readSource(path);
    if (path.extension() == ".zti") {
        PersistedInterface persisted;
        try {
            persisted = InterfaceCodec::deserialize(source);
        } catch (const InterfaceError& error) {
            throw InterfaceError(error.code(), path.string() + " : " + error.detail());
        }
        if (persisted.interface.name != name)
            throw std::runtime_error("[MOD002] " + path.string() +
                " : l'interface décrit le module '" + persisted.interface.name +
                "', module importé '" + name + "'");
        std::filesystem::path objectPath = path;
        objectPath.replace_extension(".o");
        if (!std::filesystem::exists(objectPath))
            throw std::runtime_error("[MOD003] objet précompilé manquant : " +
                                     objectPath.string());
        validatePrecompiledObject(objectPath);
        std::vector<Program::Import> imports;
        for (const std::string& import : persisted.imports)
            imports.push_back(Program::Import{SourceLocation{}, import});
        graph_.interfaces.emplace(name, std::move(persisted.interface));
        graph_.interfaceFingerprints.emplace(name, persisted.fingerprint);
        for (const Program::Import& import : imports)
            loadModule(import.module, resolveImport(import.module));
        Program program;
        if (!persisted.genericTokens.empty()) {
            Parser parser(persisted.genericTokens,
                          importedStructures(imports, graph_.interfaces),
                          importedEnumerations(imports, graph_.interfaces));
            program = parser.parse();
        }
        program.imports.clear();
        program.imports = imports;
        ModuleInterface& storedInterface = graph_.interfaces.at(name);
        for (const auto& [symbolName, symbol] : storedInterface.exports) {
            const auto existing = std::find_if(program.statements.begin(), program.statements.end(),
                [&](const Statement& statement) {
                    const auto* declaration = std::get_if<Declaration>(&statement.value);
                    return declaration != nullptr && declaration->name == symbolName;
                });
            if (existing != program.statements.end()) continue;
            std::vector<Parameter> parameters;
            for (std::size_t i = 0; i < symbol.parameterTypes.size(); ++i)
                parameters.push_back(Parameter{SourceLocation{}, "p" + std::to_string(i),
                                               symbol.parameterTypes[i]});
            program.statements.emplace_back(Declaration{SourceLocation{}, symbolName,
                symbol.type, symbol.kind, true, true, symbol.callable,
                std::move(parameters), {}, {}, nullptr});
        }
        graph_.modules.emplace(name, Module{name, path, std::move(program), hashText(source),
                                            true, objectPath, persisted.genericTokens});
        Module& storedModule = graph_.modules.at(name);
        for (Statement& statement : storedModule.program.statements) {
            auto* declaration = std::get_if<Declaration>(&statement.value);
            if (declaration != nullptr && storedInterface.exports.contains(declaration->name))
            {
                ExportedSymbol& exported = storedInterface.exports.at(declaration->name);
                exported.declaration = declaration;
                exported.type = declaration->type;
                exported.parameterTypes.clear();
                for (const Parameter& parameter : declaration->parameters)
                    exported.parameterTypes.push_back(parameter.type);
            }
        }
        loading_.erase(name);
        return;
    }
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.scan();
    const std::vector<Token> syntaxTokens = tokens;
    const std::vector<Program::Import> imports = discoverImports(tokens);
    for (const Program::Import& import : imports)
        loadModule(import.module, resolveImport(import.module));
    Parser parser(std::move(tokens), importedStructures(imports, graph_.interfaces),
                  importedEnumerations(imports, graph_.interfaces));
    Program program = parser.parse();
    graph_.modules.emplace(name, Module{name, path, std::move(program), hashText(source),
                                        false, {}, {}});
    buildInterface(name);
    std::unordered_set<std::string> genericExports;
    for (const auto& [exportName, symbol] : graph_.interfaces.at(name).exports)
        if (symbol.declaration != nullptr && !symbol.declaration->typeParameters.empty())
            genericExports.insert(exportName);
    graph_.modules.at(name).genericTokens =
        reduceGenericTokens(syntaxTokens, genericExports);
    loading_.erase(name);
}

std::filesystem::path ModuleLoader::resolveImport(const std::string& name) const {
    const std::filesystem::path local = sourceDirectory_ / (name + ".zeta");
    if (std::filesystem::exists(local)) return local;
    const std::filesystem::path localInterface = sourceDirectory_ / (name + ".zti");
    if (std::filesystem::exists(localInterface)) return localInterface;
    const std::filesystem::path sharedInterface =
        sharedLibraryDirectory_ / (name + ".zti");
    if (!sharedLibraryDirectory_.empty() && std::filesystem::exists(sharedInterface))
        return sharedInterface;
    const std::filesystem::path precompiled = standardLibraryDirectory_ / "precompiled" /
        (name + ".zti");
    if (preferPrecompiled_ && validPrecompiledModule(name) && std::filesystem::exists(precompiled))
        return precompiled;
    const std::filesystem::path standard = standardLibraryDirectory_ / (name + ".zeta");
    if (!standardLibraryDirectory_.empty() && std::filesystem::exists(standard)) return standard;
    const std::filesystem::path standardInterface = standardLibraryDirectory_ / (name + ".zti");
    if (!standardLibraryDirectory_.empty() && std::filesystem::exists(standardInterface))
        return standardInterface;
    throw std::runtime_error("[MOD001] module introuvable : " + name);
}

bool ModuleLoader::validPrecompiledModule(const std::string& name) const {
    if (standardLibraryDirectory_.empty()) return false;
    const std::filesystem::path directory = standardLibraryDirectory_ / "precompiled";
    if (!std::filesystem::exists(directory / "manifest")) return false;
    const std::string manifest = readSource(directory / "manifest");
    std::istringstream input(manifest);
    std::string key, value;
    if (!(input >> key >> value) || key != "ZETA_STDLIB" ||
        value != ZetaVersion::StdlibManifest) return false;
    if (!(input >> key >> value) || key != "compiler" || value != ZetaVersion::Compiler)
        return false;
    if (!(input >> key >> value) || key != "abi" || value != ZetaVersion::Abi) return false;
    if (!(input >> key >> value) || key != "interface" ||
        value != std::to_string(ZetaVersion::InterfaceFormat)) return false;
    std::string expectedSourceHash;
    while (input >> key) {
        if (key == "source") {
            std::string module;
            input >> module >> value;
            if (module == name) expectedSourceHash = value;
        } else {
            std::getline(input, value);
        }
    }
    if (expectedSourceHash.empty()) return false;
    const std::filesystem::path source = standardLibraryDirectory_ / (name + ".zeta");
    if (std::filesystem::exists(source) &&
        hexHash(hashText(readSource(source))) != expectedSourceHash) return false;
    return std::filesystem::exists(directory / (name + ".o"));
}

void ModuleLoader::buildFingerprints() {
    for (const auto& [name, module] : graph_.modules) {
        std::uint64_t interfaceHash = hashText("zeta-interface-v" +
            std::to_string(ZetaVersion::InterfaceFormat) + ":abi-" +
            std::string(ZetaVersion::Abi));
        std::vector<std::string> exports;
        bool exportsGenericBody = false;
        for (const auto& [symbolName, symbol] : graph_.interfaces.at(name).exports) {
            std::string signature = symbolName + ":" + typeName(symbol.type) + ":" +
                std::to_string(static_cast<int>(symbol.kind)) + ":" +
                (symbol.callable ? "call" : "value") +
                (symbol.nativeSymbol ? ":native" : ":zeta");
            for (ValueType parameter : symbol.parameterTypes)
                signature += ":" + typeName(parameter);
            if (symbol.declaration != nullptr) {
                for (std::size_t i = 0; i < symbol.declaration->typeParameters.size(); ++i) {
                    signature += ":generic:" + symbol.declaration->typeParameters[i] + ":" +
                        symbol.declaration->typeConstraints[i];
                }
                exportsGenericBody = exportsGenericBody ||
                    !symbol.declaration->typeParameters.empty();
            }
            exports.push_back(std::move(signature));
        }
        for (const std::shared_ptr<const StructType>& structure :
             graph_.interfaces.at(name).structures) {
            std::string signature = "struct:" + structure->name + ":" +
                std::to_string(structure->size) + ":" +
                std::to_string(structure->alignment);
            for (const std::string& parameter : structure->typeParameters)
                signature += ":type:" + parameter;
            for (const StructField& field : structure->fields)
                signature += ":field:" + field.name + ":" + typeName(field.type) +
                    ":" + std::to_string(field.offset);
            exports.push_back(std::move(signature));
        }
        for (const std::shared_ptr<const EnumType>& enumeration :
             graph_.interfaces.at(name).enumerations) {
            std::string signature = "enum:" + enumeration->name + ":" +
                std::to_string(enumeration->size) + ":" +
                std::to_string(enumeration->alignment) + ":" +
                std::to_string(enumeration->payloadOffset);
            for (const std::string& parameter : enumeration->typeParameters)
                signature += ":type:" + parameter;
            for (const EnumVariant& variant : enumeration->variants) {
                signature += ":variant:" + variant.name + ":" +
                    std::to_string(variant.payloadSize) + ":" +
                    std::to_string(variant.payloadAlignment);
                for (const StructField& field : variant.fields)
                    signature += ":field:" + field.name + ":" + typeName(field.type) +
                        ":" + std::to_string(field.offset);
            }
            exports.push_back(std::move(signature));
        }
        std::sort(exports.begin(), exports.end());
        for (const std::string& signature : exports)
            interfaceHash = hashText(signature, interfaceHash);
        if (exportsGenericBody)
            interfaceHash = hashText(hexHash(hashTokens(module.genericTokens)), interfaceHash);
        graph_.interfaceFingerprints.emplace(name, hexHash(interfaceHash));
    }

    for (const std::string& name : graph_.compilationOrder) {
        const Module& module = graph_.modules.at(name);
        std::uint64_t hash = module.sourceHash;
        for (const std::string& dependency : graph_.dependencies.at(name))
            hash = hashText(graph_.interfaceFingerprints.at(dependency), hash);
        hash = hashText("zeta-module-object-v" + std::string(ZetaVersion::ModuleCache) +
                        ":abi-" + std::string(ZetaVersion::Abi), hash);
        graph_.fingerprints.emplace(name, hexHash(hash));
    }
}

void ModuleLoader::buildInterfaces() {
    for (const auto& [name, module] : graph_.modules) {
        static_cast<void>(module);
        buildInterface(name);
    }
}

void ModuleLoader::buildInterface(const std::string& name) {
    if (graph_.interfaces.contains(name)) return;
    const Module& module = graph_.modules.at(name);
    ModuleInterface interface{name, {}, {}, {}};
    const auto validatePublicType = [&](const auto& self, const ValueType& type,
                                        SourceLocation location) -> void {
            if (type.kind == ValueType::Kind::Struct) {
                if (!type.structure->publicType)
                    throw CompileError(location, "le type privé '" +
                        type.structure->name + "' fuit dans l'interface publique de " + name);
                for (const ValueType& argument : type.structure->typeArguments)
                    self(self, argument, location);
                return;
            }
            if (type.kind == ValueType::Kind::Enum) {
                if (!type.enumeration->publicType)
                    throw CompileError(location, "le type privé '" +
                        type.enumeration->name +
                        "' fuit dans l'interface publique de " + name);
                for (const ValueType& argument : type.enumeration->typeArguments)
                    self(self, argument, location);
                return;
            }
            if (type.kind == ValueType::Kind::Array ||
                type.kind == ValueType::Kind::Reference ||
                type.kind == ValueType::Kind::Slice ||
                type.kind == ValueType::Kind::Box ||
                type.kind == ValueType::Kind::Vec)
                self(self, *type.element, location);
    };
    for (const std::shared_ptr<const StructType>& structure : module.program.structures) {
        if (!structure->publicType) continue;
        for (const StructField& field : structure->fields)
            validatePublicType(validatePublicType, field.type, field.location);
        interface.structures.push_back(structure);
    }
    for (const std::shared_ptr<const EnumType>& enumeration : module.program.enumerations) {
        if (!enumeration->publicType) continue;
        for (const EnumVariant& variant : enumeration->variants)
            for (const StructField& field : variant.fields)
                validatePublicType(validatePublicType, field.type, field.location);
        interface.enumerations.push_back(enumeration);
    }
    for (const Statement& statement : module.program.statements) {
        const auto* declaration = std::get_if<Declaration>(&statement.value);
        if (declaration == nullptr || !declaration->publicSymbol) continue;
        std::vector<ValueType> parameterTypes;
        validatePublicType(validatePublicType, declaration->type, declaration->location);
        for (const Parameter& parameter : declaration->parameters) {
            validatePublicType(validatePublicType, parameter.type, parameter.location);
            parameterTypes.push_back(parameter.type);
        }
        if (!interface.exports.emplace(declaration->name,
                ExportedSymbol{declaration->kind, declaration->type,
                               declaration->callable, declaration->nativeSymbol,
                               std::move(parameterTypes), declaration}).second) {
            throw CompileError(declaration->location,
                "symbole public '" + declaration->name + "' exporté plusieurs fois par " + name);
        }
    }
    graph_.interfaces.emplace(name, std::move(interface));
}

#include "module.hpp"

#include "lexer.hpp"
#include "parser.hpp"
#include "interface.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
std::string readSource(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("module introuvable : " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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
}

ModuleGraph ModuleLoader::load(const std::filesystem::path& rootPath) {
    graph_ = ModuleGraph{};
    const std::filesystem::path absolute = std::filesystem::absolute(rootPath);
    sourceDirectory_ = absolute.parent_path();
#ifdef ZETA_STDLIB_DIR
    standardLibraryDirectory_ = ZETA_STDLIB_DIR;
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

    const std::string source = readSource(path);
    if (path.extension() == ".zti") {
        PersistedInterface persisted = InterfaceCodec::deserialize(source);
        if (persisted.interface.name != name)
            throw std::runtime_error("l'interface " + path.string() +
                                     " décrit le module '" + persisted.interface.name + "'");
        std::filesystem::path objectPath = path;
        objectPath.replace_extension(".o");
        if (!std::filesystem::exists(objectPath))
            throw std::runtime_error("objet précompilé manquant : " + objectPath.string());
        Program program;
        if (!persisted.genericSource.empty()) {
            Lexer lexer(persisted.genericSource);
            Parser parser(lexer.scan());
            program = parser.parse();
        }
        program.imports.clear();
        for (const std::string& import : persisted.imports)
            program.imports.push_back(Program::Import{SourceLocation{}, import});
        for (const auto& [symbolName, symbol] : persisted.interface.exports) {
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
                                            true, objectPath, persisted.genericSource});
        ModuleInterface& storedInterface = graph_.interfaces.emplace(
            name, std::move(persisted.interface)).first->second;
        Module& storedModule = graph_.modules.at(name);
        for (Statement& statement : storedModule.program.statements) {
            auto* declaration = std::get_if<Declaration>(&statement.value);
            if (declaration != nullptr && storedInterface.exports.contains(declaration->name))
                storedInterface.exports.at(declaration->name).declaration = declaration;
        }
        graph_.interfaceFingerprints.emplace(name, persisted.fingerprint);
        for (const Program::Import& import : storedModule.program.imports)
            loadModule(import.module, resolveImport(import.module));
        return;
    }
    Lexer lexer(source);
    Parser parser(lexer.scan());
    Program program = parser.parse();
    std::vector<Program::Import> imports = program.imports;
    graph_.modules.emplace(name, Module{name, path, std::move(program), hashText(source),
                                        false, {}, source});
    for (const Program::Import& import : imports)
        loadModule(import.module, resolveImport(import.module));
}

std::filesystem::path ModuleLoader::resolveImport(const std::string& name) const {
    const std::filesystem::path local = sourceDirectory_ / (name + ".zeta");
    if (std::filesystem::exists(local)) return local;
    const std::filesystem::path localInterface = sourceDirectory_ / (name + ".zti");
    if (std::filesystem::exists(localInterface)) return localInterface;
    const std::filesystem::path standard = standardLibraryDirectory_ / (name + ".zeta");
    if (!standardLibraryDirectory_.empty() && std::filesystem::exists(standard)) return standard;
    const std::filesystem::path standardInterface = standardLibraryDirectory_ / (name + ".zti");
    if (!standardLibraryDirectory_.empty() && std::filesystem::exists(standardInterface))
        return standardInterface;
    throw std::runtime_error("module introuvable : " + name);
}

void ModuleLoader::buildFingerprints() {
    for (const auto& [name, module] : graph_.modules) {
        std::uint64_t interfaceHash = hashText("zeta-interface-v2");
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
        std::sort(exports.begin(), exports.end());
        for (const std::string& signature : exports)
            interfaceHash = hashText(signature, interfaceHash);
        if (exportsGenericBody) interfaceHash = hashText(hexHash(module.sourceHash), interfaceHash);
        graph_.interfaceFingerprints.emplace(name, hexHash(interfaceHash));
    }

    for (const std::string& name : graph_.compilationOrder) {
        const Module& module = graph_.modules.at(name);
        std::uint64_t hash = module.sourceHash;
        for (const std::string& dependency : graph_.dependencies.at(name))
            hash = hashText(graph_.interfaceFingerprints.at(dependency), hash);
        hash = hashText("zeta-module-object-v3", hash);
        graph_.fingerprints.emplace(name, hexHash(hash));
    }
}

void ModuleLoader::buildInterfaces() {
    for (const auto& [name, module] : graph_.modules) {
        if (graph_.interfaces.contains(name)) continue;
        ModuleInterface interface{name, {}};
        for (const Statement& statement : module.program.statements) {
            const auto* declaration = std::get_if<Declaration>(&statement.value);
            if (declaration == nullptr || !declaration->publicSymbol) continue;
            std::vector<ValueType> parameterTypes;
            for (const Parameter& parameter : declaration->parameters)
                parameterTypes.push_back(parameter.type);
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
}

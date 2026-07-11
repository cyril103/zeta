#include "module.hpp"

#include "lexer.hpp"
#include "parser.hpp"

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
    Lexer lexer(source);
    Parser parser(lexer.scan());
    Program program = parser.parse();
    std::vector<Program::Import> imports = program.imports;
    graph_.modules.emplace(name, Module{name, path, std::move(program), hashText(source)});
    for (const Program::Import& import : imports)
        loadModule(import.module, resolveImport(import.module));
}

std::filesystem::path ModuleLoader::resolveImport(const std::string& name) const {
    const std::filesystem::path local = sourceDirectory_ / (name + ".zeta");
    if (std::filesystem::exists(local)) return local;
    const std::filesystem::path standard = standardLibraryDirectory_ / (name + ".zeta");
    if (!standardLibraryDirectory_.empty() && std::filesystem::exists(standard)) return standard;
    throw std::runtime_error("module introuvable : " + name);
}

void ModuleLoader::buildFingerprints() {
    for (const std::string& name : graph_.compilationOrder) {
        const Module& module = graph_.modules.at(name);
        std::uint64_t hash = module.sourceHash;
        std::vector<std::string> exports;
        for (const auto& [symbolName, symbol] : graph_.interfaces.at(name).exports) {
            std::string signature = symbolName + ":" + typeName(symbol.type) + ":" +
                std::to_string(static_cast<int>(symbol.kind)) + ":" +
                (symbol.callable ? "call" : "value");
            signature += symbol.nativeSymbol ? ":native" : ":zeta";
            for (ValueType parameter : symbol.parameterTypes)
                signature += ":" + typeName(parameter);
            exports.push_back(std::move(signature));
        }
        std::sort(exports.begin(), exports.end());
        for (const std::string& signature : exports) hash = hashText(signature, hash);
        for (const std::string& dependency : graph_.dependencies.at(name))
            hash = hashText(graph_.fingerprints.at(dependency), hash);
        hash = hashText("zeta-module-cache-v1", hash);
        graph_.fingerprints.emplace(name, hexHash(hash));
    }
}

void ModuleLoader::buildInterfaces() {
    for (const auto& [name, module] : graph_.modules) {
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
                                   std::move(parameterTypes)}).second) {
                throw CompileError(declaration->location,
                    "symbole public '" + declaration->name + "' exporté plusieurs fois par " + name);
            }
        }
        graph_.interfaces.emplace(name, std::move(interface));
    }
}

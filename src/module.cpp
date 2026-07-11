#include "module.hpp"

#include "lexer.hpp"
#include "parser.hpp"

#include <cctype>
#include <fstream>
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
}

ModuleGraph ModuleLoader::load(const std::filesystem::path& rootPath) {
    graph_ = ModuleGraph{};
    const std::filesystem::path absolute = std::filesystem::absolute(rootPath);
    sourceDirectory_ = absolute.parent_path();
    graph_.root = absolute.stem().string();
    if (!validModuleName(graph_.root))
        throw std::runtime_error("nom de module invalide : " + graph_.root);
    loadModule(graph_.root, absolute);
    return std::move(graph_);
}

void ModuleLoader::loadModule(const std::string& name, const std::filesystem::path& path) {
    if (graph_.modules.contains(name)) return;
    if (!validModuleName(name)) throw std::runtime_error("nom de module invalide : " + name);

    const std::string source = readSource(path);
    Lexer lexer(source);
    Parser parser(lexer.scan());
    Program program = parser.parse();
    std::vector<Program::Import> imports = program.imports;
    graph_.modules.emplace(name, Module{name, path, std::move(program)});
    for (const Program::Import& import : imports)
        loadModule(import.module, sourceDirectory_ / (import.module + ".zeta"));
}

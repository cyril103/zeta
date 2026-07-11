#pragma once

#include "ast.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

struct Module {
    std::string name;
    std::filesystem::path path;
    Program program;
};

struct ModuleGraph {
    std::string root;
    std::unordered_map<std::string, Module> modules;
};

class ModuleLoader {
public:
    ModuleGraph load(const std::filesystem::path& rootPath);

private:
    void loadModule(const std::string& name, const std::filesystem::path& path);
    std::filesystem::path sourceDirectory_;
    ModuleGraph graph_;
};

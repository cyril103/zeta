#pragma once

#include "ast.hpp"

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ExportedSymbol {
    BindingKind kind;
    ValueType type;
    bool callable;
    std::vector<ValueType> parameterTypes;
};

struct ModuleInterface {
    std::string name;
    std::unordered_map<std::string, ExportedSymbol> exports;
};

struct Module {
    std::string name;
    std::filesystem::path path;
    Program program;
    std::uint64_t sourceHash;
};

struct ModuleGraph {
    std::string root;
    std::unordered_map<std::string, Module> modules;
    std::unordered_map<std::string, ModuleInterface> interfaces;
    std::unordered_map<std::string, std::vector<std::string>> dependencies;
    std::vector<std::string> compilationOrder;
    std::unordered_map<std::string, std::string> fingerprints;
};

class ModuleLoader {
public:
    ModuleGraph load(const std::filesystem::path& rootPath);

private:
    void loadModule(const std::string& name, const std::filesystem::path& path);
    void buildInterfaces();
    void buildDependencyGraph();
    void buildFingerprints();
    std::filesystem::path sourceDirectory_;
    ModuleGraph graph_;
};

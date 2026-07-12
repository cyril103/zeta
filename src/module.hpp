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
    bool nativeSymbol;
    std::vector<ValueType> parameterTypes;
    const Declaration* declaration;
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
    bool precompiled{false};
    std::filesystem::path objectPath;
    std::string sourceText;
};

struct ModuleGraph {
    std::string root;
    std::unordered_map<std::string, Module> modules;
    std::unordered_map<std::string, ModuleInterface> interfaces;
    std::unordered_map<std::string, std::vector<std::string>> dependencies;
    std::vector<std::string> compilationOrder;
    std::unordered_map<std::string, std::string> fingerprints;
    std::unordered_map<std::string, std::string> interfaceFingerprints;
};

class ModuleLoader {
public:
    ModuleGraph load(const std::filesystem::path& rootPath);

private:
    void loadModule(const std::string& name, const std::filesystem::path& path);
    std::filesystem::path resolveImport(const std::string& name) const;
    void buildInterfaces();
    void buildDependencyGraph();
    void buildFingerprints();
    std::filesystem::path sourceDirectory_;
    std::filesystem::path standardLibraryDirectory_;
    ModuleGraph graph_;
};

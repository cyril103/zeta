#pragma once

#include "ast.hpp"

#include <filesystem>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
    std::vector<std::shared_ptr<const StructType>> structures;
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
    explicit ModuleLoader(std::filesystem::path standardLibraryDirectory = {},
                          bool preferPrecompiled = true)
        : standardLibraryDirectory_(std::move(standardLibraryDirectory)),
          preferPrecompiled_(preferPrecompiled) {}
    ModuleGraph load(const std::filesystem::path& rootPath);

private:
    void loadModule(const std::string& name, const std::filesystem::path& path);
    std::filesystem::path resolveImport(const std::string& name) const;
    bool validPrecompiledModule(const std::string& name) const;
    void buildInterface(const std::string& name);
    void buildInterfaces();
    void buildDependencyGraph();
    void buildFingerprints();
    std::filesystem::path sourceDirectory_;
    std::filesystem::path standardLibraryDirectory_;
    ModuleGraph graph_;
    std::unordered_set<std::string> loading_;
    bool preferPrecompiled_{true};
};

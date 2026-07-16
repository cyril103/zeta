#include "codegen.hpp"
#include "ir_verifier.hpp"
#include "diagnostic.hpp"
#include "ir.hpp"
#include "interface.hpp"
#include "module.hpp"
#include "semantic.hpp"
#include "version.hpp"

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
void writeFile(const fs::path& path, const std::string& content) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("impossible d'écrire " + path.string());
    output << content;
}

std::string readOptionalFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void runFasm(const fs::path& assembly, const fs::path& executable) {
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("impossible de lancer FASM");
    if (child == 0) {
        const std::string input = assembly.string();
        const std::string output = executable.string();
        execlp("fasm", "fasm", input.c_str(), output.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status{};
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("FASM n'a pas pu produire l'exécutable");
    }
}

void runLinker(const std::vector<fs::path>& objects, const fs::path& executable) {
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("impossible de lancer ld");
    if (child == 0) {
        std::vector<std::string> arguments{"ld", "-e", "start", "-o", executable.string()};
        for (const fs::path& object : objects) arguments.push_back(object.string());
        std::vector<char*> rawArguments;
        for (std::string& argument : arguments) rawArguments.push_back(argument.data());
        rawArguments.push_back(nullptr);
        execvp("ld", rawArguments.data());
        _exit(127);
    }
    int status{};
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("ld n'a pas pu lier l'exécutable");
}

void runClang(const fs::path& llvmIr, const fs::path& executable) {
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("impossible de lancer clang");
    if (child == 0) {
        const std::string input = llvmIr.string();
        const std::string output = executable.string();
        execlp("clang", "clang", "-x", "ir", input.c_str(), "-o", output.c_str(),
               static_cast<char*>(nullptr));
        _exit(127);
    }
    int status{};
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status))
        throw std::runtime_error("clang n'a pas pu produire l'exécutable");
    if (WEXITSTATUS(status) == 127)
        throw std::runtime_error("clang introuvable pour --backend=clang");
    if (WEXITSTATUS(status) != 0)
        throw std::runtime_error("clang n'a pas pu produire l'exécutable");
}

void runRelocatableLink(const std::vector<fs::path>& objects, const fs::path& output) {
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("impossible de lancer ld -r");
    if (child == 0) {
        std::vector<std::string> arguments{"ld", "-r", "-o", output.string()};
        for (const fs::path& object : objects) arguments.push_back(object.string());
        std::vector<char*> raw;
        for (std::string& argument : arguments) raw.push_back(argument.data());
        raw.push_back(nullptr);
        execvp("ld", raw.data());
        _exit(127);
    }
    int status{};
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("ld -r n'a pas pu produire l'objet stdlib");
}

void weakenGenericSymbols(const fs::path& object, const IrProgram& ir) {
    const std::vector<std::string> definitions = IrGenerator::genericDefinitions(ir);
    if (definitions.empty()) return;
    const pid_t child = fork();
    if (child < 0)
        throw std::runtime_error("[GEN002] impossible de lancer objcopy");
    if (child == 0) {
        std::vector<std::string> arguments{"objcopy"};
        for (const std::string& definition : definitions)
            arguments.push_back("--weaken-symbol=zeta_fn_" + definition);
        arguments.push_back(object.string());
        std::vector<char*> raw;
        for (std::string& argument : arguments) raw.push_back(argument.data());
        raw.push_back(nullptr);
        execvp("objcopy", raw.data());
        _exit(127);
    }
    int status{};
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error(
            "[GEN002] objcopy n'a pas pu marquer les instances génériques");
}

void usage() {
    std::cerr << "Usage: zeta <source.zeta> [-o executable] [--stdlib dossier]"
                 " [--library-cache dossier] [--backend=fasm|clang] [--emit-llvm]\n"
                 "       zeta --build-library <source.zeta> -o dossier [--stdlib dossier]"
                 " [--library-cache dossier]\n"
                 "       zeta --install-library <module.zti> [--library-cache dossier]"
                 " [--force]\n"
                 "       zeta --build-stdlib [--stdlib dossier]\n";
}

enum class Backend { Fasm, Clang };

class TemporaryFiles {
public:
    void add(fs::path path) { paths_.push_back(std::move(path)); }
    ~TemporaryFiles() {
        for (const fs::path& path : paths_) {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    }

private:
    std::vector<fs::path> paths_;
};

bool hasNativeExports(const ModuleInterface& interface) {
    for (const auto& [name, exported] : interface.exports) {
        static_cast<void>(name);
        if (exported.nativeSymbol) return true;
    }
    return false;
}

void buildLibrary(const ModuleGraph& modules, const fs::path& outputDirectory) {
    if (fs::exists(outputDirectory) && !fs::is_directory(outputDirectory))
        throw std::runtime_error("la sortie de --build-library doit être un dossier");
    fs::create_directories(outputDirectory);

    const std::string& moduleName = modules.root;
    const Module& module = modules.modules.at(moduleName);
    if (module.precompiled)
        throw std::runtime_error("--build-library attend un module source");

    const std::string temporarySuffix = ".tmp." + std::to_string(getpid());
    const fs::path temporaryInterface =
        outputDirectory / ("." + moduleName + ".zti" + temporarySuffix);
    const fs::path temporaryAssembly =
        outputDirectory / ("." + moduleName + ".asm" + temporarySuffix);
    const fs::path temporaryObject =
        outputDirectory / ("." + moduleName + ".o" + temporarySuffix);
    const fs::path temporaryRuntime =
        outputDirectory / ("." + moduleName + ".runtime.o" + temporarySuffix);
    const fs::path temporaryLinked =
        outputDirectory / ("." + moduleName + ".linked.o" + temporarySuffix);
    TemporaryFiles temporaryFiles;
    for (const fs::path& path : {temporaryInterface, temporaryAssembly, temporaryObject,
                                 temporaryRuntime, temporaryLinked}) {
        std::error_code ignored;
        fs::remove(path, ignored);
        temporaryFiles.add(path);
    }

    IrGenerator generator;
    const IrProgram ir = generator.generateModule(modules, moduleName);
    writeFile(temporaryInterface, InterfaceCodec::serialize(
        modules.interfaces.at(moduleName),
        modules.interfaceFingerprints.at(moduleName),
        modules.dependencies.at(moduleName), module.genericTokens));
    writeFile(temporaryAssembly,
              FasmCodeGenerator::generateObject(ir, false, moduleName));
    runFasm(temporaryAssembly, temporaryObject);
    weakenGenericSymbols(temporaryObject, ir);

    fs::path publishedObject = temporaryObject;
    if (hasNativeExports(modules.interfaces.at(moduleName))) {
#ifdef ZETA_RUNTIME_DIR
        const fs::path runtimeAssembly = fs::path(ZETA_RUNTIME_DIR) / (moduleName + ".asm");
#else
        const fs::path runtimeAssembly;
#endif
        if (readOptionalFile(runtimeAssembly).empty())
            throw std::runtime_error("runtime natif introuvable pour le module " + moduleName);
        runFasm(runtimeAssembly, temporaryRuntime);
        runRelocatableLink({temporaryObject, temporaryRuntime}, temporaryLinked);
        publishedObject = temporaryLinked;
    }

    const fs::path interfacePath = outputDirectory / (moduleName + ".zti");
    const fs::path objectPath = outputDirectory / (moduleName + ".o");
    fs::rename(temporaryInterface, interfacePath);
    fs::rename(publishedObject, objectPath);
}

fs::path defaultLibraryCache() {
    if (const char* configured = std::getenv("ZETA_LIBRARY_CACHE"))
        if (*configured != '\0') return configured;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"))
        if (*xdg != '\0') return fs::path(xdg) / "zeta" / "libraries";
    if (const char* home = std::getenv("HOME"))
        if (*home != '\0') return fs::path(home) / ".cache" / "zeta" / "libraries";
    throw std::runtime_error("[LIB001] emplacement du cache de bibliothèques inconnu");
}

fs::path libraryCacheDirectory(const fs::path& configured) {
    const fs::path root = configured.empty() ? defaultLibraryCache() : configured;
    return root / ("abi-" + std::string(ZetaVersion::Abi));
}

PersistedInterface validateInstalledDependency(const fs::path& cache,
                                               const std::string& moduleName) {
    const fs::path interfacePath = cache / (moduleName + ".zti");
    const fs::path objectPath = cache / (moduleName + ".o");
    if (!fs::is_regular_file(interfacePath) || !fs::is_regular_file(objectPath))
        throw std::runtime_error("[LIB002] dépendance '" + moduleName +
                                 "' absente du cache partagé");
    try {
        PersistedInterface dependency =
            InterfaceCodec::deserialize(readOptionalFile(interfacePath));
        if (dependency.interface.name != moduleName)
            throw std::runtime_error("nom de module incohérent");
        validatePrecompiledObject(objectPath);
        return dependency;
    } catch (const std::exception& error) {
        throw std::runtime_error("[LIB002] dépendance '" + moduleName +
                                 "' invalide : " + error.what());
    }
}

void installLibrary(const fs::path& interfaceSource, const fs::path& configuredCache,
                    bool force) {
    if (interfaceSource.extension() != ".zti")
        throw std::runtime_error("[LIB001] --install-library attend un fichier .zti");
    fs::path objectSource = interfaceSource;
    objectSource.replace_extension(".o");
    if (!fs::is_regular_file(interfaceSource) || !fs::is_regular_file(objectSource))
        throw std::runtime_error("[LIB001] paire .zti/.o incomplète pour " +
                                 interfaceSource.stem().string());

    PersistedInterface persisted;
    try {
        persisted = InterfaceCodec::deserialize(readOptionalFile(interfaceSource));
    } catch (const InterfaceError& error) {
        throw InterfaceError(error.code(), interfaceSource.string() + " : " + error.detail());
    }
    const std::string moduleName = interfaceSource.stem().string();
    if (persisted.interface.name != moduleName)
        throw std::runtime_error("[LIB001] l'interface décrit le module '" +
            persisted.interface.name + "', fichier '" + moduleName + "'");
    validatePrecompiledObject(objectSource);

    const fs::path cache = libraryCacheDirectory(configuredCache);
    fs::create_directories(cache);
    for (const std::string& dependency : persisted.imports)
        validateInstalledDependency(cache, dependency);

    const fs::path interfaceTarget = cache / (moduleName + ".zti");
    const fs::path objectTarget = cache / (moduleName + ".o");
    const bool hadInterface = fs::exists(interfaceTarget);
    const bool hadObject = fs::exists(objectTarget);
    if ((hadInterface || hadObject) && !force) {
        if (!hadInterface || !hadObject)
            throw std::runtime_error("[LIB003] installation existante incomplète pour '" +
                                     moduleName + "' ; utiliser --force");
        try {
            const PersistedInterface installed =
                InterfaceCodec::deserialize(readOptionalFile(interfaceTarget));
            validatePrecompiledObject(objectTarget);
            if (installed.interface.name != moduleName ||
                installed.fingerprint != persisted.fingerprint)
                throw std::runtime_error("empreinte différente");
        } catch (const std::exception&) {
            throw std::runtime_error("[LIB003] conflit d'empreinte pour '" + moduleName +
                                     "' ; utiliser --force");
        }
    }

    const std::string suffix = ".tmp." + std::to_string(getpid());
    const fs::path interfaceTemporary = cache / ("." + moduleName + ".zti" + suffix);
    const fs::path objectTemporary = cache / ("." + moduleName + ".o" + suffix);
    const fs::path interfaceBackup = cache / ("." + moduleName + ".zti.bak" + suffix);
    const fs::path objectBackup = cache / ("." + moduleName + ".o.bak" + suffix);
    TemporaryFiles temporaryFiles;
    for (const fs::path& path : {interfaceTemporary, objectTemporary,
                                 interfaceBackup, objectBackup}) {
        std::error_code ignored;
        fs::remove(path, ignored);
        temporaryFiles.add(path);
    }
    fs::copy_file(interfaceSource, interfaceTemporary);
    fs::copy_file(objectSource, objectTemporary);
    validatePrecompiledObject(objectTemporary);

    if (hadInterface) fs::rename(interfaceTarget, interfaceBackup);
    if (hadObject) fs::rename(objectTarget, objectBackup);
    try {
        fs::rename(interfaceTemporary, interfaceTarget);
        fs::rename(objectTemporary, objectTarget);
    } catch (...) {
        std::error_code ignored;
        fs::remove(interfaceTarget, ignored);
        fs::remove(objectTarget, ignored);
        if (hadInterface) fs::rename(interfaceBackup, interfaceTarget);
        if (hadObject) fs::rename(objectBackup, objectTarget);
        throw;
    }
    std::error_code ignored;
    fs::remove(interfaceBackup, ignored);
    fs::remove(objectBackup, ignored);
}

std::string startAssembly(const ModuleGraph& modules) {
    std::string assembly = "format ELF64\nsection '.text' executable\npublic start\n";
    for (const std::string& module : modules.compilationOrder)
        assembly += "extrn zeta_init_" + module + "\n";
    assembly += "extrn zeta_fn_" + modules.root + "__main\nstart:\n";
    for (const std::string& module : modules.compilationOrder)
        assembly += "    call zeta_init_" + module + "\n";
    assembly += "    call zeta_fn_" + modules.root + "__main\n"
                "    mov edi, eax\n"
                "    mov eax, 60\n"
                "    syscall\n";
    return assembly;
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }

    fs::path sourcePath;
    fs::path outputPath;
    fs::path standardLibraryPath;
    fs::path libraryCachePath;
    bool buildStandardLibrary = false;
    bool buildLibraryModule = false;
    bool installLibraryModule = false;
    bool forceLibraryInstall = false;
    bool emitLlvm = false;
    Backend backend = Backend::Fasm;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--build-stdlib") {
            buildStandardLibrary = true;
        } else if (argument == "--build-library") {
            buildLibraryModule = true;
        } else if (argument == "--install-library") {
            installLibraryModule = true;
        } else if (argument == "--force") {
            forceLibraryInstall = true;
        } else if (argument == "--emit-llvm") {
            emitLlvm = true;
            backend = Backend::Clang;
        } else if (argument.rfind("--backend=", 0) == 0) {
            const std::string selectedBackend = argument.substr(std::string("--backend=").size());
            if (selectedBackend == "fasm") backend = Backend::Fasm;
            else if (selectedBackend == "clang") backend = Backend::Clang;
            else {
                std::cerr << "Erreur: backend inconnu '" << selectedBackend << "'\n";
                return 2;
            }
        } else if (argument == "-o") {
            if (++i >= argc) {
                usage();
                return 2;
            }
            outputPath = argv[i];
        } else if (argument == "--stdlib") {
            if (++i >= argc) {
                usage();
                return 2;
            }
            standardLibraryPath = argv[i];
        } else if (argument == "--library-cache") {
            if (++i >= argc) {
                usage();
                return 2;
            }
            libraryCachePath = argv[i];
        } else if (sourcePath.empty()) {
            sourcePath = argument;
        } else {
            usage();
            return 2;
        }
    }
    if (standardLibraryPath.empty()) {
#ifdef ZETA_STDLIB_DIR
        standardLibraryPath = ZETA_STDLIB_DIR;
#endif
    }
    const int selectedModes = static_cast<int>(buildStandardLibrary) +
        static_cast<int>(buildLibraryModule) + static_cast<int>(installLibraryModule);
    if (selectedModes > 1) {
        usage();
        return 2;
    }
    if (forceLibraryInstall && !installLibraryModule) {
        usage();
        return 2;
    }
    if ((buildStandardLibrary || buildLibraryModule || installLibraryModule) &&
        (emitLlvm || backend == Backend::Clang)) {
        std::cerr << "Erreur: --backend=clang et --emit-llvm sont réservés aux exécutables\n";
        return 2;
    }
    if (emitLlvm && backend == Backend::Fasm) {
        std::cerr << "Erreur: --emit-llvm requiert le backend clang\n";
        return 2;
    }
    if (buildLibraryModule &&
        (sourcePath.empty() || outputPath.empty() || sourcePath.extension() != ".zeta")) {
        usage();
        return 2;
    }
    std::vector<std::string> standardModules;
    if (buildStandardLibrary) {
        if (standardLibraryPath.empty())
            throw std::runtime_error("dossier de bibliothèque standard inconnu");
        for (const fs::directory_entry& entry : fs::directory_iterator(standardLibraryPath))
            if (entry.is_regular_file() && entry.path().extension() == ".zeta")
                standardModules.push_back(entry.path().stem().string());
        std::sort(standardModules.begin(), standardModules.end());
        const fs::path precompiled = standardLibraryPath / "precompiled";
        fs::create_directories(precompiled);
        sourcePath = precompiled / "__stdlib_build.zeta";
        std::string driver;
        for (const std::string& module : standardModules) driver += "import " + module + "\n";
        driver += "def main(): Int = 0\n";
        writeFile(sourcePath, driver);
        outputPath = precompiled / ".stdlib-build";
    }
    if (sourcePath.empty()) {
        usage();
        return 2;
    }
    if (installLibraryModule) {
        if (!outputPath.empty() || sourcePath.extension() != ".zti") {
            usage();
            return 2;
        }
        try {
            installLibrary(sourcePath, libraryCachePath, forceLibraryInstall);
            std::cout << "Bibliothèque installée : " <<
                libraryCacheDirectory(libraryCachePath) / sourcePath.filename() << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Erreur: " << error.what() << '\n';
            return 1;
        }
    }
    if (outputPath.empty()) {
        outputPath = sourcePath;
        outputPath.replace_extension();
    }

    try {
        const fs::path sharedLibraryDirectory = buildStandardLibrary
            ? fs::path{} : libraryCacheDirectory(libraryCachePath);
        ModuleLoader loader(standardLibraryPath, !buildStandardLibrary,
                            sharedLibraryDirectory);
        ModuleGraph modules = loader.load(sourcePath);
        for (const std::string& moduleName : modules.compilationOrder) {
            if (modules.modules.at(moduleName).precompiled &&
                modules.modules.at(moduleName).genericTokens.empty()) continue;
            SemanticAnalyzer semanticAnalyzer;
            semanticAnalyzer.analyze(modules.modules.at(moduleName).program, &modules.interfaces,
                                     moduleName == modules.root && !buildLibraryModule);
        }
        if (buildLibraryModule) {
            buildLibrary(modules, outputPath);
            std::cout << "Bibliothèque créée : " << outputPath / (modules.root + ".zti")
                      << " et " << outputPath / (modules.root + ".o") << '\n';
            return 0;
        }
        IrGenerator irGenerator;
        const IrProgram ir = irGenerator.generate(modules);
        const VerifiedIrProgram verifiedIr =
            IrVerifier::verify(ir, IrVerificationMode::Executable);

        std::unordered_map<std::string, IrProgram> moduleIrs;
        std::unordered_map<std::string, std::string> claimedGenericInstances;
        for (const std::string& moduleName : modules.compilationOrder) {
            if (modules.modules.at(moduleName).precompiled) continue;
            IrGenerator moduleGenerator;
            IrProgram moduleIr = moduleGenerator.generateModule(modules, moduleName);
            std::unordered_set<std::string> duplicates;
            for (const auto& [definition, identity] :
                 IrGenerator::genericDefinitionIdentities(moduleIr)) {
                const auto [existing, inserted] =
                    claimedGenericInstances.emplace(definition, identity);
                if (!inserted && existing->second != identity)
                    throw std::runtime_error(
                        "[GEN001] collision de symbole générique canonique '" +
                        definition + "'");
                if (!inserted) duplicates.insert(definition);
            }
            IrGenerator::removeGenericDefinitions(moduleIr, duplicates);
            moduleIrs.emplace(moduleName, std::move(moduleIr));
        }

        fs::path irPath = outputPath;
        irPath += ".ir";
        fs::path llvmIrPath = outputPath;
        llvmIrPath += ".ll";
        if (emitLlvm) {
            writeFile(llvmIrPath, LlvmIrCodeGenerator::generate(verifiedIr));
            return 0;
        }
        if (backend == Backend::Clang) {
            writeFile(irPath, IrGenerator::print(verifiedIr));
            writeFile(llvmIrPath, LlvmIrCodeGenerator::generate(verifiedIr));
            runClang(llvmIrPath, outputPath);
            std::cout << "IR créé          : " << irPath << '\n'
                      << "LLVM IR créé     : " << llvmIrPath << '\n'
                      << "Executable créé : " << outputPath << '\n';
            return 0;
        }
        fs::path assemblyPath = outputPath;
        assemblyPath += ".asm";
        writeFile(irPath, IrGenerator::print(verifiedIr));
        writeFile(assemblyPath, FasmCodeGenerator::generate(verifiedIr));

        fs::path cacheDirectory = outputPath;
        cacheDirectory += ".cache";
        fs::create_directories(cacheDirectory);

        fs::path moduleDirectory = outputPath;
        moduleDirectory += ".modules";
        fs::create_directories(moduleDirectory);
        const fs::path startSource = moduleDirectory / "start.asm";
        const fs::path startObject = moduleDirectory / "start.o";
        const fs::path cachedStartObject = cacheDirectory / "start.o";
        const fs::path startStamp = cacheDirectory / "start.stamp";
        std::string startFingerprint = "zeta-start-v" +
            std::string(ZetaVersion::StartCache) + ":abi-" +
            std::string(ZetaVersion::Abi) + ":" + modules.root;
        for (const std::string& moduleName : modules.compilationOrder)
            startFingerprint += ":" + moduleName;
        if (!fs::exists(cachedStartObject) ||
            readOptionalFile(startStamp) != startFingerprint) {
            writeFile(startSource, startAssembly(modules));
            runFasm(startSource, cachedStartObject);
            writeFile(startStamp, startFingerprint);
        }
        fs::copy_file(cachedStartObject, startObject, fs::copy_options::overwrite_existing);
        std::vector<fs::path> objects{startObject};
        for (const std::string& moduleName : modules.compilationOrder) {
            const Module& module = modules.modules.at(moduleName);
            if (module.precompiled) {
                const fs::path moduleObject = moduleDirectory / (moduleName + ".o");
                const fs::path moduleInterface = moduleDirectory / (moduleName + ".zti");
                fs::copy_file(module.objectPath, moduleObject,
                              fs::copy_options::overwrite_existing);
                fs::copy_file(module.path, moduleInterface,
                              fs::copy_options::overwrite_existing);
                objects.push_back(moduleObject);
                continue;
            }
            const IrProgram& moduleIr = moduleIrs.at(moduleName);
            const VerifiedIrProgram verifiedModuleIr =
                IrVerifier::verify(moduleIr, IrVerificationMode::ModuleObject);
            const fs::path moduleIrPath = moduleDirectory / (moduleName + ".ir");
            const fs::path moduleInterfacePath = moduleDirectory / (moduleName + ".zti");
            const fs::path moduleAssembly = moduleDirectory / (moduleName + ".asm");
            const fs::path moduleObject = moduleDirectory / (moduleName + ".o");
            const fs::path cachedModuleObject = cacheDirectory / (moduleName + ".o");
            const fs::path moduleStamp = cacheDirectory / (moduleName + ".stamp");
            const std::string fingerprint = "zeta-module-object-v" +
                std::string(ZetaVersion::ModuleCache) + ":" +
                modules.fingerprints.at(moduleName);
            std::string objectFingerprint = fingerprint;
            for (const std::string& definition : IrGenerator::genericDefinitions(moduleIr))
                objectFingerprint += ":generic-owner:" + definition;
            writeFile(moduleIrPath, IrGenerator::print(verifiedModuleIr));
            writeFile(moduleInterfacePath, InterfaceCodec::serialize(
                modules.interfaces.at(moduleName),
                modules.interfaceFingerprints.at(moduleName),
                modules.dependencies.at(moduleName),
                modules.modules.at(moduleName).genericTokens));
            if (!fs::exists(cachedModuleObject) ||
                readOptionalFile(moduleStamp) != objectFingerprint) {
                writeFile(moduleAssembly,
                    FasmCodeGenerator::generateObject(
                        verifiedModuleIr, false, moduleName));
                runFasm(moduleAssembly, cachedModuleObject);
                weakenGenericSymbols(cachedModuleObject, moduleIr);
                writeFile(moduleStamp, objectFingerprint);
            }
            fs::copy_file(cachedModuleObject, moduleObject, fs::copy_options::overwrite_existing);
            objects.push_back(moduleObject);

            bool hasNativeExports = false;
            for (const auto& [exportName, exported] : modules.interfaces.at(moduleName).exports) {
                static_cast<void>(exportName);
                hasNativeExports = hasNativeExports || exported.nativeSymbol;
            }
            if (hasNativeExports) {
#ifdef ZETA_RUNTIME_DIR
                const fs::path runtimeAssembly = fs::path(ZETA_RUNTIME_DIR) / (moduleName + ".asm");
#else
                const fs::path runtimeAssembly;
#endif
                const std::string runtimeSource = readOptionalFile(runtimeAssembly);
                if (runtimeSource.empty())
                    throw std::runtime_error("runtime natif introuvable pour le module " + moduleName);
                const fs::path cachedRuntimeObject = cacheDirectory / ("runtime-" + moduleName + ".o");
                const fs::path runtimeStamp = cacheDirectory / ("runtime-" + moduleName + ".stamp");
                const std::string runtimeFingerprint = fingerprint + ":" + runtimeSource;
                if (!fs::exists(cachedRuntimeObject) ||
                    readOptionalFile(runtimeStamp) != runtimeFingerprint) {
                    runFasm(runtimeAssembly, cachedRuntimeObject);
                    writeFile(runtimeStamp, runtimeFingerprint);
                }
                fs::copy_file(cachedRuntimeObject, moduleDirectory / (moduleName + ".runtime.o"),
                              fs::copy_options::overwrite_existing);
                objects.push_back(moduleDirectory / (moduleName + ".runtime.o"));
            }
        }
        runLinker(objects, outputPath);
        fs::permissions(outputPath,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add);

        if (buildStandardLibrary) {
            const fs::path precompiled = standardLibraryPath / "precompiled";
            for (const std::string& module : standardModules) {
                fs::copy_file(moduleDirectory / (module + ".zti"),
                              precompiled / (module + ".zti"),
                              fs::copy_options::overwrite_existing);
                const fs::path runtimeObject = moduleDirectory / (module + ".runtime.o");
                if (fs::exists(runtimeObject))
                    runRelocatableLink({moduleDirectory / (module + ".o"), runtimeObject},
                                       precompiled / (module + ".o"));
                else
                    fs::copy_file(moduleDirectory / (module + ".o"),
                                  precompiled / (module + ".o"),
                                  fs::copy_options::overwrite_existing);
            }
            std::string manifest = "ZETA_STDLIB " + std::string(ZetaVersion::StdlibManifest) +
                "\ncompiler " + std::string(ZetaVersion::Compiler) +
                "\nabi " + std::string(ZetaVersion::Abi) +
                "\ninterface " + std::to_string(ZetaVersion::InterfaceFormat) + "\n";
            for (const std::string& module : standardModules) manifest += "module " + module + "\n";
            for (const std::string& module : standardModules) {
                std::uint64_t hash = 1469598103934665603ULL;
                for (const unsigned char byte : readOptionalFile(standardLibraryPath / (module + ".zeta"))) {
                    hash ^= byte;
                    hash *= 1099511628211ULL;
                }
                std::ostringstream encoded;
                encoded << std::hex << std::setfill('0') << std::setw(16) << hash;
                manifest += "source " + module + " " + encoded.str() + "\n";
            }
            writeFile(precompiled / "manifest", manifest);
            fs::remove(sourcePath);
            std::cout << "Bibliothèque standard précompilée : " << precompiled << '\n';
            fs::remove(outputPath);
            fs::path temporary = outputPath;
            temporary += ".ir";
            fs::remove(temporary);
            temporary = outputPath;
            temporary += ".asm";
            fs::remove(temporary);
            fs::remove_all(cacheDirectory);
            fs::remove_all(moduleDirectory);
        }

        if (!buildStandardLibrary)
            std::cout << "IR créé          : " << irPath << '\n'
                      << "Assembleur créé : " << assemblyPath << '\n'
                      << "Objets ELF64 créés : " << moduleDirectory << '\n'
                      << "Executable créé : " << outputPath << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Erreur: " << error.what() << '\n';
        return 1;
    }
}

#include "codegen.hpp"
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
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

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

void usage() {
    std::cerr << "Usage: zeta <source.zeta> [-o executable] [--stdlib dossier]\n"
                 "       zeta --build-stdlib [--stdlib dossier]\n";
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
    bool buildStandardLibrary = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--build-stdlib") {
            buildStandardLibrary = true;
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
    if (outputPath.empty()) {
        outputPath = sourcePath;
        outputPath.replace_extension();
    }

    try {
        ModuleLoader loader(standardLibraryPath, !buildStandardLibrary);
        ModuleGraph modules = loader.load(sourcePath);
        for (const std::string& moduleName : modules.compilationOrder) {
            if (modules.modules.at(moduleName).precompiled &&
                modules.modules.at(moduleName).genericTokens.empty()) continue;
            SemanticAnalyzer semanticAnalyzer;
            semanticAnalyzer.analyze(modules.modules.at(moduleName).program, &modules.interfaces,
                                     moduleName == modules.root);
        }
        IrGenerator irGenerator;
        const IrProgram ir = irGenerator.generate(modules);

        fs::path irPath = outputPath;
        irPath += ".ir";
        fs::path assemblyPath = outputPath;
        assemblyPath += ".asm";
        writeFile(irPath, IrGenerator::print(ir));
        writeFile(assemblyPath, FasmCodeGenerator::generate(ir));

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
            IrGenerator moduleGenerator;
            const IrProgram moduleIr = moduleGenerator.generateModule(modules, moduleName);
            const fs::path moduleIrPath = moduleDirectory / (moduleName + ".ir");
            const fs::path moduleInterfacePath = moduleDirectory / (moduleName + ".zti");
            const fs::path moduleAssembly = moduleDirectory / (moduleName + ".asm");
            const fs::path moduleObject = moduleDirectory / (moduleName + ".o");
            const fs::path cachedModuleObject = cacheDirectory / (moduleName + ".o");
            const fs::path moduleStamp = cacheDirectory / (moduleName + ".stamp");
            const std::string fingerprint = "zeta-module-object-v" +
                std::string(ZetaVersion::ModuleCache) + ":" +
                modules.fingerprints.at(moduleName);
            writeFile(moduleIrPath, IrGenerator::print(moduleIr));
            writeFile(moduleInterfacePath, InterfaceCodec::serialize(
                modules.interfaces.at(moduleName),
                modules.interfaceFingerprints.at(moduleName),
                modules.dependencies.at(moduleName),
                modules.modules.at(moduleName).genericTokens));
            if (!fs::exists(cachedModuleObject) || readOptionalFile(moduleStamp) != fingerprint) {
                writeFile(moduleAssembly,
                    FasmCodeGenerator::generateObject(moduleIr, false, moduleName));
                runFasm(moduleAssembly, cachedModuleObject);
                writeFile(moduleStamp, fingerprint);
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

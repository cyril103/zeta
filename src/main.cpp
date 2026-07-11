#include "codegen.hpp"
#include "diagnostic.hpp"
#include "ir.hpp"
#include "module.hpp"
#include "semantic.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
void writeFile(const fs::path& path, const std::string& content) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("impossible d'écrire " + path.string());
    output << content;
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

void usage() {
    std::cerr << "Usage: zeta <source.zeta> [-o executable]\n";
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }

    fs::path sourcePath;
    fs::path outputPath;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-o") {
            if (++i >= argc) {
                usage();
                return 2;
            }
            outputPath = argv[i];
        } else if (sourcePath.empty()) {
            sourcePath = argument;
        } else {
            usage();
            return 2;
        }
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
        ModuleLoader loader;
        ModuleGraph modules = loader.load(sourcePath);
        for (const std::string& moduleName : modules.compilationOrder) {
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

        fs::path objectAssemblyPath = outputPath;
        objectAssemblyPath += ".object.asm";
        fs::path objectPath = outputPath;
        objectPath += ".o";
        writeFile(objectAssemblyPath, FasmCodeGenerator::generateObject(ir));
        runFasm(objectAssemblyPath, objectPath);

        fs::path moduleDirectory = outputPath;
        moduleDirectory += ".modules";
        fs::create_directories(moduleDirectory);
        for (const std::string& moduleName : modules.compilationOrder) {
            const std::string symbol = "zeta_module_" + moduleName;
            const fs::path moduleAssembly = moduleDirectory / (moduleName + ".asm");
            const fs::path moduleObject = moduleDirectory / (moduleName + ".o");
            writeFile(moduleAssembly,
                "format ELF64\nsection '.rodata'\npublic " + symbol + "\n" + symbol + ": db 0\n");
            runFasm(moduleAssembly, moduleObject);
        }
        runFasm(assemblyPath, outputPath);
        fs::permissions(outputPath,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add);

        std::cout << "IR créé          : " << irPath << '\n'
                  << "Assembleur créé : " << assemblyPath << '\n'
                  << "Objet ELF64 créé : " << objectPath << '\n'
                  << "Executable créé : " << outputPath << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Erreur: " << error.what() << '\n';
        return 1;
    }
}

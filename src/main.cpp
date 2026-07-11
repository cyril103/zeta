#include "codegen.hpp"
#include "diagnostic.hpp"
#include "ir.hpp"
#include "lexer.hpp"
#include "parser.hpp"
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
std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("impossible d'ouvrir " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

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
        const std::string source = readFile(sourcePath);
        Lexer lexer(source);
        Parser parser(lexer.scan());
        Program program = parser.parse();
        SemanticAnalyzer semanticAnalyzer;
        const TypedProgram typedProgram = semanticAnalyzer.analyze(program);
        IrGenerator irGenerator;
        const IrProgram ir = irGenerator.generate(typedProgram);

        fs::path irPath = outputPath;
        irPath += ".ir";
        fs::path assemblyPath = outputPath;
        assemblyPath += ".asm";
        writeFile(irPath, IrGenerator::print(ir));
        writeFile(assemblyPath, FasmCodeGenerator::generate(ir));
        runFasm(assemblyPath, outputPath);
        fs::permissions(outputPath,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add);

        std::cout << "IR créé          : " << irPath << '\n'
                  << "Assembleur créé : " << assemblyPath << '\n'
                  << "Executable créé : " << outputPath << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Erreur: " << error.what() << '\n';
        return 1;
    }
}

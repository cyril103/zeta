#pragma once

#include "ir.hpp"

#include <string>

class FasmCodeGenerator {
public:
    static std::string generate(const IrProgram& program);
    static std::string generate(const VerifiedIrProgram& program);
    static std::string generateObject(const IrProgram& program, bool entryPoint = true,
                                      const std::string& initializer = {});
    static std::string generateObject(const VerifiedIrProgram& program,
                                      bool entryPoint = true,
                                      const std::string& initializer = {});

private:
    static std::string generateUnchecked(const IrProgram& program);
};

class LlvmIrCodeGenerator {
public:
    static std::string generate(const IrProgram& program);
    static std::string generate(const VerifiedIrProgram& program);
    static std::string generateObject(const IrProgram& program);
    static std::string generateObject(const VerifiedIrProgram& program);
};

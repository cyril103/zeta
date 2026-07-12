#pragma once

#include "ir.hpp"

#include <string>

class FasmCodeGenerator {
public:
    static std::string generate(const IrProgram& program);
    static std::string generateObject(const IrProgram& program, bool entryPoint = true,
                                      const std::string& initializer = {});
};

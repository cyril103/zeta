#pragma once

#include "ir.hpp"

#include <stdexcept>
#include <string>

enum class IrVerificationMode { Executable, ModuleObject };

class IrVerificationError : public std::runtime_error {
public:
    IrVerificationError(std::string code, const std::string& message);

    const std::string& code() const noexcept { return code_; }

private:
    std::string code_;
};

class IrVerifier {
public:
    static void verify(const IrProgram& program, IrVerificationMode mode);
};

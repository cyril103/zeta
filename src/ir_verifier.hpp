#pragma once

#include "ir.hpp"

#include <stdexcept>
#include <string>

enum class IrVerificationMode { Executable, ModuleObject };

class VerifiedIrProgram {
public:
    const IrProgram& program() const noexcept { return *program_; }
    IrVerificationMode mode() const noexcept { return mode_; }

private:
    friend class IrVerifier;
    VerifiedIrProgram(const IrProgram& program, IrVerificationMode mode)
        : program_(&program), mode_(mode) {}

    const IrProgram* program_;
    IrVerificationMode mode_;
};

class IrVerificationError : public std::runtime_error {
public:
    IrVerificationError(std::string code, const std::string& message);

    const std::string& code() const noexcept { return code_; }

private:
    std::string code_;
};

class IrVerifier {
public:
    static VerifiedIrProgram verify(const IrProgram& program, IrVerificationMode mode);
    static VerifiedIrProgram verify(const IrProgram&& program, IrVerificationMode mode) = delete;
};

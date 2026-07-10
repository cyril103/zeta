#pragma once

#include <stdexcept>
#include <string>

struct SourceLocation {
    std::size_t line{1};
    std::size_t column{1};
};

class CompileError : public std::runtime_error {
public:
    CompileError(SourceLocation location, const std::string& message)
        : std::runtime_error("ligne " + std::to_string(location.line) +
                             ", colonne " + std::to_string(location.column) +
                             " : " + message) {}
};

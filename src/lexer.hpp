#pragma once

#include "token.hpp"

#include <string_view>
#include <vector>

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}
    std::vector<Token> scan();

private:
    bool atEnd() const;
    char peek(std::size_t offset = 0) const;
    char advance();
    void add(TokenKind kind, std::string text, SourceLocation location);

    std::string_view source_;
    std::size_t current_{0};
    SourceLocation location_{};
    std::vector<Token> tokens_;
};

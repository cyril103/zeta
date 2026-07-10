#pragma once

#include "ast.hpp"
#include "token.hpp"

#include <vector>

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
    Program parse();

private:
    const Token& peek() const;
    const Token& previous() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token& consume(TokenKind kind, const std::string& message);
    void skipSeparators();
    Statement statement();
    Declaration declaration(BindingKind kind);
    Assignment assignment();
    ExprPtr expression();
    ExprPtr addition();
    ExprPtr multiplication();
    ExprPtr unary();
    ExprPtr primary();

    std::vector<Token> tokens_;
    std::size_t current_{0};
};

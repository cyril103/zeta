#pragma once

#include "ast.hpp"
#include "token.hpp"

#include <vector>
#include <unordered_set>
#include <unordered_map>

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
    Program parse();

private:
    const Token& peek() const;
    const Token& previous() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    bool checkSeparator() const;
    bool matchSeparator();
    bool startsAssignment() const;
    const Token& consume(TokenKind kind, const std::string& message);
    ValueType consumeType(const std::string& message);
    std::shared_ptr<StructType> structure();
    void skipSeparators();
    void expressionContinuation();
    Statement statement();
    Declaration declaration(BindingKind kind);
    std::string qualifiedName();
    Statement assignment();
    Statement dereferenceAssignment();
    WhileStatement whileStatement();
    std::vector<StatementPtr> loopBody();
    ExprPtr expression();
    ExprPtr logicalOr();
    ExprPtr logicalAnd();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr addition();
    ExprPtr multiplication();
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary();
    ExprPtr blockExpression(SourceLocation location);
    ExprPtr ifExpression(SourceLocation location);

    std::vector<Token> tokens_;
    std::size_t current_{0};
    std::size_t blockDepth_{0};
    bool publicDeclaration_{false};
    bool nativeDeclaration_{false};
    std::unordered_set<std::string> activeTypeParameters_;
    std::unordered_map<std::string, std::shared_ptr<StructType>> structures_;
    std::unordered_set<std::string> importedModules_;
};

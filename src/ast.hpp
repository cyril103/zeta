#pragma once

#include "diagnostic.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct Expression;
using ExprPtr = std::unique_ptr<Expression>;

struct IntegerExpr { std::int32_t value; };
struct NameExpr { std::string name; };
struct UnaryExpr { char op; ExprPtr operand; };
struct BinaryExpr { char op; ExprPtr left; ExprPtr right; };

struct Expression {
    SourceLocation location;
    std::variant<IntegerExpr, NameExpr, UnaryExpr, BinaryExpr> value;
};

enum class BindingKind { Val, Var, Def };

struct Declaration {
    SourceLocation location;
    std::string name;
    std::string type;
    BindingKind kind;
    ExprPtr initializer;
};

struct Assignment {
    SourceLocation location;
    std::string name;
    ExprPtr value;
};

using Statement = std::variant<Declaration, Assignment>;

struct Program {
    std::vector<Statement> statements;
};

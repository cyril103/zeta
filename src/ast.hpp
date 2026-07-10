#pragma once

#include "diagnostic.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

enum class ValueType { Int, Byte };

inline std::string typeName(ValueType type) {
    return type == ValueType::Int ? "Int" : "Byte";
}

struct Expression;
struct Statement;
using ExprPtr = std::unique_ptr<Expression>;
using StatementPtr = std::unique_ptr<Statement>;

struct IntegerExpr { std::int32_t value; };
struct NameExpr { std::string name; };
struct CallExpr { std::string name; std::vector<ExprPtr> arguments; };
struct UnaryExpr { char op; ExprPtr operand; };
struct BinaryExpr { char op; ExprPtr left; ExprPtr right; };
struct BlockExpr { std::vector<StatementPtr> statements; ExprPtr result; };

struct Expression {
    SourceLocation location;
    std::variant<IntegerExpr, NameExpr, CallExpr, UnaryExpr, BinaryExpr, BlockExpr> value;
    mutable ValueType inferredType{ValueType::Int};
};

enum class BindingKind { Val, Var, Def };

struct Parameter {
    SourceLocation location;
    std::string name;
    ValueType type;
};

struct Declaration {
    SourceLocation location;
    std::string name;
    ValueType type;
    BindingKind kind;
    bool callable;
    std::vector<Parameter> parameters;
    ExprPtr initializer;
};

struct Assignment {
    SourceLocation location;
    std::string name;
    ExprPtr value;
};

struct Statement {
    std::variant<Declaration, Assignment> value;

    Statement(Declaration declaration) : value(std::move(declaration)) {}
    Statement(Assignment assignment) : value(std::move(assignment)) {}
};

struct Program {
    std::vector<Statement> statements;
};

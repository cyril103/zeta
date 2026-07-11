#pragma once

#include "diagnostic.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

enum class ValueType { Int, Byte, Double, Bool };

inline std::string typeName(ValueType type) {
    if (type == ValueType::Int) return "Int";
    if (type == ValueType::Byte) return "Byte";
    if (type == ValueType::Double) return "Double";
    return "Bool";
}

struct Expression;
struct Statement;
using ExprPtr = std::unique_ptr<Expression>;
using StatementPtr = std::unique_ptr<Statement>;

struct IntegerExpr { std::int32_t value; };
struct DoubleExpr { double value; };
struct BoolExpr { bool value; };
struct NameExpr { std::string name; };
struct CallExpr { std::string name; std::vector<ExprPtr> arguments; };
struct ConversionExpr { ValueType target; ExprPtr operand; };
struct UnaryExpr { std::string op; ExprPtr operand; };
struct BinaryExpr { std::string op; ExprPtr left; ExprPtr right; };
struct BlockExpr { std::vector<StatementPtr> statements; ExprPtr result; };
struct IfExpr { ExprPtr condition; ExprPtr thenBranch; ExprPtr elseBranch; };

struct Expression {
    SourceLocation location;
    std::variant<IntegerExpr, DoubleExpr, BoolExpr, NameExpr, CallExpr, ConversionExpr,
                 UnaryExpr, BinaryExpr, BlockExpr, IfExpr> value;
    ValueType inferredType{ValueType::Int};
    bool typed{false};
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

struct WhileStatement {
    SourceLocation location;
    ExprPtr condition;
    std::vector<StatementPtr> body;
};

struct ExpressionStatement {
    SourceLocation location;
    ExprPtr expression;
};
struct ReturnStatement { SourceLocation location; ExprPtr value; };

struct Statement {
    std::variant<Declaration, Assignment, WhileStatement, ExpressionStatement, ReturnStatement> value;

    Statement(Declaration declaration) : value(std::move(declaration)) {}
    Statement(Assignment assignment) : value(std::move(assignment)) {}
    Statement(WhileStatement loop) : value(std::move(loop)) {}
    Statement(ExpressionStatement expression) : value(std::move(expression)) {}
    Statement(ReturnStatement statement) : value(std::move(statement)) {}
};

struct Program {
    std::vector<Statement> statements;
};

#pragma once

#include "diagnostic.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

enum class ValueType { Int, Byte, Double, Bool, Char, String };

inline std::string typeName(ValueType type) {
    if (type == ValueType::Int) return "Int";
    if (type == ValueType::Byte) return "Byte";
    if (type == ValueType::Double) return "Double";
    if (type == ValueType::Bool) return "Bool";
    if (type == ValueType::Char) return "Char";
    return "String";
}

struct Expression;
struct Statement;
using ExprPtr = std::unique_ptr<Expression>;
using StatementPtr = std::unique_ptr<Statement>;

struct IntegerExpr { std::int32_t value; };
struct DoubleExpr { double value; };
struct BoolExpr { bool value; };
struct CharacterExpr { std::uint32_t value; };
struct StringExpr { std::string utf8; };
struct NameExpr { std::string name; };
struct CallExpr { std::string name; std::vector<ExprPtr> arguments; };
struct ConversionExpr { ValueType target; ExprPtr operand; };
struct UnaryExpr { std::string op; ExprPtr operand; };
struct BinaryExpr { std::string op; ExprPtr left; ExprPtr right; };
struct BlockExpr { std::vector<StatementPtr> statements; ExprPtr result; };
struct IfExpr { ExprPtr condition; ExprPtr thenBranch; ExprPtr elseBranch; };

struct Expression {
    SourceLocation location;
    std::variant<IntegerExpr, DoubleExpr, BoolExpr, CharacterExpr, StringExpr, NameExpr, CallExpr, ConversionExpr,
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
    bool publicSymbol;
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
struct BreakStatement { SourceLocation location; };
struct ContinueStatement { SourceLocation location; };

struct Statement {
    std::variant<Declaration, Assignment, WhileStatement, ExpressionStatement, ReturnStatement,
                 BreakStatement, ContinueStatement> value;

    Statement(Declaration declaration) : value(std::move(declaration)) {}
    Statement(Assignment assignment) : value(std::move(assignment)) {}
    Statement(WhileStatement loop) : value(std::move(loop)) {}
    Statement(ExpressionStatement expression) : value(std::move(expression)) {}
    Statement(ReturnStatement statement) : value(std::move(statement)) {}
    Statement(BreakStatement statement) : value(statement) {}
    Statement(ContinueStatement statement) : value(statement) {}
};

struct Program {
    struct Import {
        SourceLocation location;
        std::string module;
    };
    std::vector<Import> imports;
    std::vector<Statement> statements;
};

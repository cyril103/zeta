#pragma once

#include "diagnostic.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct ValueType {
    enum class Kind { Int, Byte, Double, Bool, Char, String, Array, Reference };

    Kind kind;
    std::shared_ptr<const ValueType> element;
    std::size_t length{0};
    bool mutableReference{false};

    explicit ValueType(Kind primitive) : kind(primitive) {}
    ValueType(std::shared_ptr<const ValueType> elementType, std::size_t arrayLength)
        : kind(Kind::Array), element(std::move(elementType)), length(arrayLength) {}
    ValueType(std::shared_ptr<const ValueType> referencedType, bool mutableBorrow)
        : kind(Kind::Reference), element(std::move(referencedType)),
          mutableReference(mutableBorrow) {}

    static const ValueType Int;
    static const ValueType Byte;
    static const ValueType Double;
    static const ValueType Bool;
    static const ValueType Char;
    static const ValueType String;

    friend bool operator==(const ValueType& left, const ValueType& right) {
        if (left.kind != right.kind) return false;
        if (left.kind != Kind::Array && left.kind != Kind::Reference) return true;
        if (left.element == nullptr || right.element == nullptr || *left.element != *right.element)
            return false;
        return left.kind == Kind::Array ? left.length == right.length
                                        : left.mutableReference == right.mutableReference;
    }
};

inline const ValueType ValueType::Int{ValueType::Kind::Int};
inline const ValueType ValueType::Byte{ValueType::Kind::Byte};
inline const ValueType ValueType::Double{ValueType::Kind::Double};
inline const ValueType ValueType::Bool{ValueType::Kind::Bool};
inline const ValueType ValueType::Char{ValueType::Kind::Char};
inline const ValueType ValueType::String{ValueType::Kind::String};

inline std::string typeName(ValueType type) {
    if (type == ValueType::Int) return "Int";
    if (type == ValueType::Byte) return "Byte";
    if (type == ValueType::Double) return "Double";
    if (type == ValueType::Bool) return "Bool";
    if (type == ValueType::Char) return "Char";
    if (type == ValueType::String) return "String";
    if (type.kind == ValueType::Kind::Array)
        return "[" + typeName(*type.element) + "; " + std::to_string(type.length) + "]";
    return type.mutableReference ? "&mut " + typeName(*type.element)
                                 : "&" + typeName(*type.element);
}

inline std::size_t valueTypeSize(const ValueType& type) {
    if (type == ValueType::Byte || type == ValueType::Bool) return 1U;
    if (type == ValueType::Int || type == ValueType::Char) return 4U;
    if (type == ValueType::Double) return 8U;
    if (type == ValueType::String) return 16U;
    if (type.kind == ValueType::Kind::Reference) return 8U;
    return valueTypeSize(*type.element) * type.length;
}

inline std::size_t valueTypeAlignment(const ValueType& type) {
    if (type == ValueType::Byte || type == ValueType::Bool) return 1U;
    if (type == ValueType::Int || type == ValueType::Char) return 4U;
    if (type == ValueType::Double || type == ValueType::String) return 8U;
    if (type.kind == ValueType::Kind::Reference) return 8U;
    return valueTypeAlignment(*type.element);
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
struct ArrayExpr { std::vector<ExprPtr> elements; };
struct IndexExpr { ExprPtr array; ExprPtr index; };
struct NameExpr { std::string name; };
struct CallExpr { std::string name; std::vector<ExprPtr> arguments; };
struct ConversionExpr { ValueType target; ExprPtr operand; };
struct UnaryExpr { std::string op; ExprPtr operand; };
struct BinaryExpr { std::string op; ExprPtr left; ExprPtr right; };
struct BlockExpr { std::vector<StatementPtr> statements; ExprPtr result; };
struct IfExpr { ExprPtr condition; ExprPtr thenBranch; ExprPtr elseBranch; };

struct Expression {
    SourceLocation location;
    std::variant<IntegerExpr, DoubleExpr, BoolExpr, CharacterExpr, StringExpr, ArrayExpr, IndexExpr, NameExpr, CallExpr, ConversionExpr,
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
    bool nativeSymbol;
    bool callable;
    std::vector<Parameter> parameters;
    ExprPtr initializer;
};

struct Assignment {
    SourceLocation location;
    std::string name;
    ExprPtr value;
};
struct IndexAssignment {
    SourceLocation location;
    std::string name;
    std::vector<ExprPtr> indexes;
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
    std::variant<Declaration, Assignment, IndexAssignment, WhileStatement, ExpressionStatement, ReturnStatement,
                 BreakStatement, ContinueStatement> value;

    Statement(Declaration declaration) : value(std::move(declaration)) {}
    Statement(Assignment assignment) : value(std::move(assignment)) {}
    Statement(IndexAssignment assignment) : value(std::move(assignment)) {}
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

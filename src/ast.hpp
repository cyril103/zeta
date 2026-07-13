#pragma once

#include "diagnostic.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct StructType;
struct EnumType;

struct ValueType {
    enum class Kind { Int, Byte, Double, Bool, Char, String, StringView, Array, Reference, Slice, Box, TypeParameter, Struct, Enum };

    Kind kind;
    std::shared_ptr<const ValueType> element;
    std::size_t length{0};
    bool mutableReference{false};
    std::string typeParameter;
    std::shared_ptr<const StructType> structure;
    std::shared_ptr<const EnumType> enumeration;

    explicit ValueType(Kind primitive) : kind(primitive) {}
    ValueType(std::shared_ptr<const ValueType> elementType, std::size_t arrayLength)
        : kind(Kind::Array), element(std::move(elementType)), length(arrayLength) {}
    ValueType(std::shared_ptr<const ValueType> referencedType, bool mutableBorrow)
        : kind(Kind::Reference), element(std::move(referencedType)),
          mutableReference(mutableBorrow) {}
    ValueType(Kind viewKind, std::shared_ptr<const ValueType> elementType,
              bool mutableView)
        : kind(viewKind), element(std::move(elementType)),
          mutableReference(mutableView) {}
    ValueType(Kind ownerKind, std::shared_ptr<const ValueType> elementType)
        : kind(ownerKind), element(std::move(elementType)) {}
    ValueType(Kind parameterKind, std::string parameterName)
        : kind(parameterKind), typeParameter(std::move(parameterName)) {}
    explicit ValueType(std::shared_ptr<const StructType> structType)
        : kind(Kind::Struct), structure(std::move(structType)) {}
    explicit ValueType(std::shared_ptr<const EnumType> enumType)
        : kind(Kind::Enum), enumeration(std::move(enumType)) {}

    static const ValueType Int;
    static const ValueType Byte;
    static const ValueType Double;
    static const ValueType Bool;
    static const ValueType Char;
    static const ValueType String;
    static const ValueType StringView;

    friend bool operator==(const ValueType& left, const ValueType& right) {
        if (left.kind != right.kind) return false;
        if (left.kind != Kind::Array && left.kind != Kind::Reference &&
            left.kind != Kind::Slice && left.kind != Kind::Box &&
            left.kind != Kind::TypeParameter && left.kind != Kind::Struct &&
            left.kind != Kind::Enum) return true;
        if (left.kind == Kind::Struct) return left.structure == right.structure;
        if (left.kind == Kind::Enum) return left.enumeration == right.enumeration;
        if (left.kind == Kind::TypeParameter)
            return left.typeParameter == right.typeParameter;
        if (left.element == nullptr || right.element == nullptr || *left.element != *right.element)
            return false;
        return left.kind == Kind::Array ? left.length == right.length
                                        : left.mutableReference == right.mutableReference;
    }
};

struct StructField {
    SourceLocation location;
    std::string name;
    ValueType type;
    std::size_t offset{0};
};

struct StructType {
    SourceLocation location;
    std::string name;
    std::vector<std::string> typeParameters;
    std::vector<ValueType> typeArguments;
    std::vector<StructField> fields;
    std::size_t size{0};
    std::size_t alignment{1};
};

struct EnumVariant {
    SourceLocation location;
    std::string name;
    std::vector<StructField> fields;
    std::size_t payloadSize{0};
    std::size_t payloadAlignment{1};
};

struct EnumType {
    SourceLocation location;
    std::string name;
    std::vector<std::string> typeParameters;
    std::vector<ValueType> typeArguments;
    std::shared_ptr<const EnumType> genericDefinition;
    mutable std::unordered_map<std::string, std::weak_ptr<const EnumType>> instances;
    std::vector<EnumVariant> variants;
    std::size_t payloadOffset{4};
    std::size_t size{4};
    std::size_t alignment{4};
};

std::shared_ptr<const EnumType> instantiateEnumType(
    const std::shared_ptr<const EnumType>& enumeration,
    std::vector<ValueType> arguments,
    SourceLocation location);

inline bool isCopyValueType(const ValueType& type) {
    if (type.kind == ValueType::Kind::Box ||
        type.kind == ValueType::Kind::TypeParameter)
        return false;
    if (type.kind == ValueType::Kind::Reference || type.kind == ValueType::Kind::Slice)
        return !type.mutableReference;
    if (type.kind == ValueType::Kind::Array)
        return isCopyValueType(*type.element);
    if (type.kind == ValueType::Kind::Struct) {
        for (const StructField& field : type.structure->fields)
            if (!isCopyValueType(field.type)) return false;
    }
    if (type.kind == ValueType::Kind::Enum) {
        for (const EnumVariant& variant : type.enumeration->variants)
            for (const StructField& field : variant.fields)
                if (!isCopyValueType(field.type)) return false;
    }
    return true;
}

inline bool valueTypeNeedsDrop(const ValueType& type) {
    if (type == ValueType::String || type.kind == ValueType::Kind::Box) return true;
    if (type.kind == ValueType::Kind::Array)
        return valueTypeNeedsDrop(*type.element);
    if (type.kind == ValueType::Kind::Struct) {
        for (const StructField& field : type.structure->fields)
            if (valueTypeNeedsDrop(field.type)) return true;
    }
    if (type.kind == ValueType::Kind::Enum) {
        for (const EnumVariant& variant : type.enumeration->variants)
            for (const StructField& field : variant.fields)
                if (valueTypeNeedsDrop(field.type)) return true;
    }
    return false;
}

inline bool isMoveOnlyValueType(const ValueType& type) {
    if (type.kind == ValueType::Kind::Box) return true;
    if (type.kind == ValueType::Kind::Array)
        return !isCopyValueType(*type.element);
    if (type.kind == ValueType::Kind::Struct || type.kind == ValueType::Kind::Enum)
        return !isCopyValueType(type);
    return false;
}

inline bool isEquatableValueType(const ValueType& type) {
    if (type == ValueType::Int || type == ValueType::Byte ||
        type == ValueType::Double || type == ValueType::Bool ||
        type == ValueType::Char || type == ValueType::String ||
        type == ValueType::StringView)
        return true;
    if (type.kind == ValueType::Kind::Array)
        return isEquatableValueType(*type.element);
    if (type.kind == ValueType::Kind::Struct) {
        for (const StructField& field : type.structure->fields)
            if (!isEquatableValueType(field.type)) return false;
        return true;
    }
    if (type.kind == ValueType::Kind::Enum) {
        for (const EnumVariant& variant : type.enumeration->variants)
            for (const StructField& field : variant.fields)
                if (!isEquatableValueType(field.type)) return false;
        return true;
    }
    return false;
}

inline const ValueType ValueType::Int{ValueType::Kind::Int};
inline const ValueType ValueType::Byte{ValueType::Kind::Byte};
inline const ValueType ValueType::Double{ValueType::Kind::Double};
inline const ValueType ValueType::Bool{ValueType::Kind::Bool};
inline const ValueType ValueType::Char{ValueType::Kind::Char};
inline const ValueType ValueType::String{ValueType::Kind::String};
inline const ValueType ValueType::StringView{ValueType::Kind::StringView};

inline std::string typeName(ValueType type) {
    if (type == ValueType::Int) return "Int";
    if (type == ValueType::Byte) return "Byte";
    if (type == ValueType::Double) return "Double";
    if (type == ValueType::Bool) return "Bool";
    if (type == ValueType::Char) return "Char";
    if (type == ValueType::String) return "String";
    if (type == ValueType::StringView) return "StringView";
    if (type.kind == ValueType::Kind::Array)
        return "[" + typeName(*type.element) + "; " + std::to_string(type.length) + "]";
    if (type.kind == ValueType::Kind::Slice)
        return std::string(type.mutableReference ? "SliceMut[" : "Slice[") +
               typeName(*type.element) + "]";
    if (type.kind == ValueType::Kind::Box)
        return "Box[" + typeName(*type.element) + "]";
    if (type.kind == ValueType::Kind::TypeParameter) return type.typeParameter;
    if (type.kind == ValueType::Kind::Struct) {
        std::string name = type.structure->name;
        if (!type.structure->typeArguments.empty()) {
            name += '[';
            for (std::size_t i = 0; i < type.structure->typeArguments.size(); ++i) {
                if (i != 0) name += ", ";
                name += typeName(type.structure->typeArguments[i]);
            }
            name += ']';
        }
        return name;
    }
    if (type.kind == ValueType::Kind::Enum) {
        std::string name = type.enumeration->name;
        if (!type.enumeration->typeArguments.empty()) {
            name += '[';
            for (std::size_t i = 0; i < type.enumeration->typeArguments.size(); ++i) {
                if (i != 0) name += ", ";
                name += typeName(type.enumeration->typeArguments[i]);
            }
            name += ']';
        }
        return name;
    }
    return type.mutableReference ? "&mut " + typeName(*type.element)
                                 : "&" + typeName(*type.element);
}

inline std::size_t valueTypeSize(const ValueType& type) {
    if (type.kind == ValueType::Kind::TypeParameter) return 0U;
    if (type == ValueType::Byte || type == ValueType::Bool) return 1U;
    if (type == ValueType::Int || type == ValueType::Char) return 4U;
    if (type == ValueType::Double) return 8U;
    if (type == ValueType::String || type == ValueType::StringView ||
        type.kind == ValueType::Kind::Slice) return 16U;
    if (type.kind == ValueType::Kind::Reference || type.kind == ValueType::Kind::Box) return 8U;
    if (type.kind == ValueType::Kind::Struct) return type.structure->size;
    if (type.kind == ValueType::Kind::Enum) return type.enumeration->size;
    return valueTypeSize(*type.element) * type.length;
}

inline std::size_t valueTypeAlignment(const ValueType& type) {
    if (type.kind == ValueType::Kind::TypeParameter) return 1U;
    if (type == ValueType::Byte || type == ValueType::Bool) return 1U;
    if (type == ValueType::Int || type == ValueType::Char) return 4U;
    if (type == ValueType::Double || type == ValueType::String ||
        type == ValueType::StringView ||
        type.kind == ValueType::Kind::Slice) return 8U;
    if (type.kind == ValueType::Kind::Reference || type.kind == ValueType::Kind::Box) return 8U;
    if (type.kind == ValueType::Kind::Struct) return type.structure->alignment;
    if (type.kind == ValueType::Kind::Enum) return type.enumeration->alignment;
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
struct StructExpr { std::shared_ptr<const StructType> type; std::vector<ExprPtr> fields; };
struct EnumExpr {
    std::shared_ptr<const EnumType> type;
    std::size_t variant;
    std::vector<ExprPtr> fields;
};
struct FieldExpr { ExprPtr object; std::string field; };
struct IndexExpr { ExprPtr array; ExprPtr index; };
struct AddressExpr { bool mutableBorrow; ExprPtr operand; };
struct DereferenceExpr { ExprPtr operand; };
struct NameExpr { std::string name; };
struct CallExpr {
    std::string name;
    std::vector<ValueType> typeArguments;
    std::vector<ExprPtr> arguments;
};
struct ConversionExpr { ValueType target; ExprPtr operand; };
struct UnaryExpr { std::string op; ExprPtr operand; };
struct BinaryExpr { std::string op; ExprPtr left; ExprPtr right; };
struct BlockExpr { std::vector<StatementPtr> statements; ExprPtr result; };
struct IfExpr { ExprPtr condition; ExprPtr thenBranch; ExprPtr elseBranch; };
struct MatchBranch {
    SourceLocation location;
    std::size_t variant;
    std::vector<std::optional<std::string>> bindings;
    ExprPtr result;
};
struct MatchExpr {
    ExprPtr operand;
    std::shared_ptr<const EnumType> type;
    std::vector<MatchBranch> branches;
};

struct Expression {
    SourceLocation location;
    std::variant<IntegerExpr, DoubleExpr, BoolExpr, CharacterExpr, StringExpr, ArrayExpr, StructExpr, EnumExpr, FieldExpr, IndexExpr, AddressExpr, DereferenceExpr, NameExpr, CallExpr, ConversionExpr,
                 UnaryExpr, BinaryExpr, BlockExpr, IfExpr, MatchExpr> value;
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
    std::vector<std::string> typeParameters;
    std::vector<std::string> typeConstraints;
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
struct FieldAssignment { SourceLocation location; std::string name; std::string field; ExprPtr value; };
struct DereferenceAssignment {
    SourceLocation location;
    ExprPtr reference;
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
    std::variant<Declaration, Assignment, IndexAssignment, FieldAssignment, DereferenceAssignment, WhileStatement, ExpressionStatement, ReturnStatement,
                 BreakStatement, ContinueStatement> value;

    Statement(Declaration declaration) : value(std::move(declaration)) {}
    Statement(Assignment assignment) : value(std::move(assignment)) {}
    Statement(IndexAssignment assignment) : value(std::move(assignment)) {}
    Statement(FieldAssignment assignment) : value(std::move(assignment)) {}
    Statement(DereferenceAssignment assignment) : value(std::move(assignment)) {}
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
    std::vector<std::shared_ptr<const StructType>> structures;
    std::vector<std::shared_ptr<const EnumType>> enumerations;
    std::vector<Statement> statements;
};

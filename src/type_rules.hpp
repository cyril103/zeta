#pragma once

#include "ast.hpp"

#include <string_view>

namespace TypeRules {

inline bool isLogical(std::string_view op) { return op == "&&" || op == "||"; }
inline bool isEquality(std::string_view op) { return op == "==" || op == "!="; }
inline bool isOrdering(std::string_view op) {
    return op == "<" || op == "<=" || op == ">" || op == ">=";
}
inline bool isComparison(std::string_view op) { return isEquality(op) || isOrdering(op); }
inline bool isNumeric(ValueType type) {
    return type == ValueType::Int || type == ValueType::Byte || type == ValueType::Double;
}
inline bool canExplicitlyConvert(ValueType source, ValueType target) {
    return (isNumeric(source) && isNumeric(target)) ||
           (source == ValueType::Char && target == ValueType::Int) ||
           (source == ValueType::Int && target == ValueType::Char) || source == target;
}

inline ValueType commonOperandType(ValueType left, ValueType right) {
    if (left == right) return left;
    if (left == ValueType::Double || right == ValueType::Double) return ValueType::Double;
    if (left == ValueType::Byte || right == ValueType::Byte) return ValueType::Byte;
    if (left == ValueType::Bool || right == ValueType::Bool) return ValueType::Bool;
    return left;
}

} // namespace TypeRules

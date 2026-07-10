#pragma once

#include "ast.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using ValueId = std::size_t;
using SlotId = std::size_t;

struct IrConst { ValueId output; std::int32_t value; ValueType type; };
struct IrDoubleConst { ValueId output; double value; };
struct IrLoad { ValueId output; SlotId slot; ValueType type; };
struct IrUnary { ValueId output; char op; ValueId operand; ValueType type; };
struct IrBinary { ValueId output; char op; ValueId left; ValueId right; ValueType type; };
struct IrStore { SlotId slot; ValueId value; ValueType type; };
using IrInstruction = std::variant<IrConst, IrDoubleConst, IrLoad, IrUnary, IrBinary, IrStore>;

struct IrSlot { std::string name; ValueType type; };
struct IrProgram {
    std::vector<IrSlot> slots;
    std::vector<IrInstruction> instructions;
    std::vector<ValueType> valueTypes;
    std::size_t valueCount{0};
    ValueId exitValue{0};
};

class IrGenerator {
public:
    IrProgram generate(const Program& program);
    static std::string print(const IrProgram& program);

private:
    struct Symbol {
        SlotId slot;
        BindingKind kind;
        const Declaration* declaration;
    };

    ValueType validateExpression(
        const Expression& expression,
        ValueType expected,
        const std::unordered_map<std::string, ValueType>& parameters = {}) const;
    ValueType validateExpression(
        const Expression& expression,
        ValueType expected,
        const std::unordered_map<std::string, ValueType>& parameters,
        const std::unordered_map<std::string, const Declaration*>& locals) const;
    ValueId expression(const Expression& expression);
    ValueId expression(const Expression& expression,
                       const std::unordered_map<std::string, ValueId>& parameters);
    ValueId nextValue(ValueType type);
    IrProgram ir_;
    std::unordered_map<std::string, Symbol> symbols_;
};

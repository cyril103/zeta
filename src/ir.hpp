#pragma once

#include "ast.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

using ValueId = std::size_t;
using SlotId = std::size_t;

struct IrConst { ValueId output; std::int32_t value; };
struct IrLoad { ValueId output; SlotId slot; };
struct IrUnary { ValueId output; char op; ValueId operand; };
struct IrBinary { ValueId output; char op; ValueId left; ValueId right; };
struct IrStore { SlotId slot; ValueId value; };
using IrInstruction = std::variant<IrConst, IrLoad, IrUnary, IrBinary, IrStore>;

struct IrSlot { std::string name; std::string type; };
struct IrProgram {
    std::vector<IrSlot> slots;
    std::vector<IrInstruction> instructions;
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

    void validateExpression(const Expression& expression,
                            const std::unordered_set<std::string>& parameters = {}) const;
    void validateExpression(
        const Expression& expression,
        const std::unordered_set<std::string>& parameters,
        const std::unordered_map<std::string, const Declaration*>& locals) const;
    ValueId expression(const Expression& expression);
    ValueId expression(const Expression& expression,
                       const std::unordered_map<std::string, ValueId>& parameters);
    ValueId nextValue();
    IrProgram ir_;
    std::unordered_map<std::string, Symbol> symbols_;
};

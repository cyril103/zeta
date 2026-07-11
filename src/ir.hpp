#pragma once

#include "ast.hpp"
#include "semantic.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <utility>
#include <vector>

using ValueId = std::size_t;
using SlotId = std::size_t;

struct IrConst { ValueId output; std::int32_t value; ValueType type; };
struct IrDoubleConst { ValueId output; double value; };
struct IrStringConst { ValueId output; std::string utf8; };
struct IrLoad { ValueId output; SlotId slot; ValueType type; };
struct IrConvert { ValueId output; ValueId input; ValueType source; ValueType target; };
struct IrUnary { ValueId output; std::string op; ValueId operand; ValueType type; };
struct IrBinary {
    ValueId output;
    std::string op;
    ValueId left;
    ValueId right;
    ValueType type;
    ValueType operandType;
};
struct IrStore { SlotId slot; ValueId value; ValueType type; };
struct IrCopy { ValueId output; ValueId input; ValueType type; };
struct IrCall {
    ValueId output;
    std::string function;
    std::vector<ValueId> arguments;
    std::vector<ValueType> argumentTypes;
    ValueType returnType;
};
struct IrTailCall {
    std::string function;
    std::vector<ValueId> arguments;
    std::vector<ValueType> argumentTypes;
};
struct IrFunctionStart { std::string name; };
struct IrParameter { ValueId output; std::size_t index; std::size_t stackOffset; ValueType type; };
struct IrReturn { ValueId value; ValueType type; };
struct IrExit { ValueId value; };
struct IrBranch { ValueId condition; bool jumpWhenTrue; std::size_t label; };
struct IrJump { std::size_t label; };
struct IrLabel { std::size_t label; };
using IrInstruction = std::variant<IrConst, IrDoubleConst, IrStringConst, IrLoad, IrConvert, IrUnary, IrBinary,
                                   IrStore, IrCopy, IrCall, IrTailCall, IrFunctionStart, IrParameter,
                                   IrReturn, IrExit, IrBranch, IrJump, IrLabel>;

struct IrSlot { std::string name; ValueType type; bool global; };
struct IrProgram {
    std::vector<IrSlot> slots;
    std::vector<IrInstruction> instructions;
    std::vector<ValueType> valueTypes;
    std::size_t valueCount{0};
    ValueId exitValue{0};
};

class IrGenerator {
public:
    IrProgram generate(const TypedProgram& program);
    static std::string print(const IrProgram& program);

private:
    struct Symbol {
        SlotId slot;
        BindingKind kind;
        const Declaration* declaration;
        bool global;
    };

    void emitLoop(const WhileStatement& loop,
                  const std::unordered_map<std::string, ValueId>& parameters);
    ValueId expression(const Expression& expression);
    ValueId expression(const Expression& expression,
                       const std::unordered_map<std::string, ValueId>& parameters);
    void emitTailExpression(const Expression& expression,
                            const std::unordered_map<std::string, ValueId>& parameters,
                            const Declaration& function);
    ValueId nextValue(ValueType type);
    IrProgram ir_;
    std::unordered_map<std::string, Symbol> symbols_;
    bool inFunction_{false};
    std::vector<std::pair<std::size_t, std::size_t>> loopLabels_;
    std::size_t nextLabel_{0};
};

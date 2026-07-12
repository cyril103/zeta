#pragma once

#include "ast.hpp"
#include "semantic.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <utility>
#include <vector>

using ValueId = std::size_t;
using SlotId = std::size_t;

struct IrConst { ValueId output; std::int32_t value; ValueType type; };
struct IrDoubleConst { ValueId output; double value; };
struct IrStringConst { ValueId output; std::string utf8; };
struct IrArrayConstruct { ValueId output; std::vector<ValueId> elements; ValueType type; };
struct IrStructConstruct { ValueId output; std::vector<ValueId> fields; ValueType type; };
struct IrFieldLoad { ValueId output; ValueId object; ValueType objectType; std::size_t field; };
struct IrFieldStore { SlotId slot; ValueId value; ValueType objectType; std::size_t field; };
struct IrSliceConstruct { ValueId output; ValueId reference; std::size_t length; ValueType type; };
struct IrBoxConstruct { ValueId output; ValueId value; ValueType elementType; };
struct IrIndexLoad {
    ValueId output;
    ValueId array;
    ValueId index;
    ValueType arrayType;
    bool arrayIsReference;
    bool arrayIsSlice;
};
struct IrIndexStore {
    SlotId slot;
    ValueId array;
    std::vector<ValueId> indexes;
    ValueId value;
    ValueType arrayType;
    bool arrayIsReference;
    bool arrayIsSlice;
};
struct IrAddressOf { ValueId output; SlotId slot; };
struct IrDereference { ValueId output; ValueId reference; ValueType type; };
struct IrDereferenceStore { ValueId reference; ValueId value; ValueType type; };
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
struct IrDrop { ValueId value; ValueType type; };
struct IrRetain { ValueId value; ValueType type; };
struct IrExit { ValueId value; };
struct IrBranch { ValueId condition; bool jumpWhenTrue; std::size_t label; };
struct IrJump { std::size_t label; };
struct IrLabel { std::size_t label; };
using IrInstruction = std::variant<IrConst, IrDoubleConst, IrStringConst, IrArrayConstruct, IrStructConstruct, IrFieldLoad, IrFieldStore, IrSliceConstruct, IrBoxConstruct, IrIndexLoad, IrIndexStore, IrAddressOf, IrDereference, IrDereferenceStore, IrLoad, IrConvert, IrUnary, IrBinary,
                                   IrStore, IrCopy, IrCall, IrTailCall, IrFunctionStart, IrParameter,
                                   IrReturn, IrDrop, IrRetain, IrExit, IrBranch, IrJump, IrLabel>;

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
    IrProgram generate(const ModuleGraph& graph);
    static std::string print(const IrProgram& program);

private:
    struct GenericInstance {
        const Declaration* declaration;
        std::vector<ValueType> types;
        std::string linkName;
    };
    struct Symbol {
        SlotId slot;
        BindingKind kind;
        const Declaration* declaration;
        bool global;
        std::string linkName;
    };

    void emitLoop(const WhileStatement& loop,
                  const std::unordered_map<std::string, ValueId>& parameters);
    void emitIndexStore(const IndexAssignment& assignment,
                        const std::unordered_map<std::string, ValueId>& parameters);
    void emitFieldStore(const FieldAssignment& assignment,
                        const std::unordered_map<std::string, ValueId>& parameters);
    void emitBoxDrops(const std::vector<std::string>& names);
    void emitAllBoxDrops();
    void emitBoxParameterDrops();
    ValueId expression(const Expression& expression);
    ValueId expression(const Expression& expression,
                       const std::unordered_map<std::string, ValueId>& parameters);
    void emitTailExpression(const Expression& expression,
                            const std::unordered_map<std::string, ValueId>& parameters,
                            const Declaration& function);
    ValueId nextValue(ValueType type);
    ValueType resolveType(const ValueType& type) const;
    std::string genericLinkName(const Declaration& declaration,
                                const std::vector<ValueType>& types) const;
    void registerGenericInstance(const Declaration& declaration,
                                 const std::vector<ValueType>& types);
    IrProgram ir_;
    std::unordered_map<std::string, Symbol> symbols_;
    bool inFunction_{false};
    std::vector<std::pair<std::size_t, std::size_t>> loopLabels_;
    std::size_t nextLabel_{0};
    std::unordered_set<std::string> movedBoxes_;
    std::vector<std::vector<std::string>> boxScopes_;
    std::unordered_map<std::string, std::pair<ValueId, ValueType>> boxParameters_;
    std::unordered_map<std::string, ValueType> typeSubstitutions_;
    std::vector<GenericInstance> genericInstances_;
    std::unordered_set<std::string> genericInstanceNames_;
};

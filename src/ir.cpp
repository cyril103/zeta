#include "ir.hpp"

#include "diagnostic.hpp"

#include <sstream>
#include <type_traits>

ValueId IrGenerator::nextValue() { return ir_.valueCount++; }

IrProgram IrGenerator::generate(const Program& program) {
    for (const Declaration& declaration : program.declarations) {
        if (symbols_.contains(declaration.name)) {
            throw CompileError(declaration.location,
                               "la val '" + declaration.name + "' est déjà définie");
        }
        // The initializer cannot refer to the value currently being declared.
        const ValueId value = expression(*declaration.initializer);
        const SlotId slot = ir_.slots.size();
        symbols_.emplace(declaration.name, slot);
        ir_.slots.push_back(IrSlot{declaration.name, declaration.type});
        ir_.instructions.push_back(IrStore{slot, value});
    }
    return std::move(ir_);
}

ValueId IrGenerator::expression(const Expression& expressionNode) {
    return std::visit([&](const auto& node) -> ValueId {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IntegerExpr>) {
            const ValueId output = nextValue();
            ir_.instructions.push_back(IrConst{output, node.value});
            return output;
        } else if constexpr (std::is_same_v<T, NameExpr>) {
            const auto found = symbols_.find(node.name);
            if (found == symbols_.end()) {
                throw CompileError(expressionNode.location,
                                   "val inconnue '" + node.name + "'");
            }
            const ValueId output = nextValue();
            ir_.instructions.push_back(IrLoad{output, found->second});
            return output;
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            const ValueId operand = expression(*node.operand);
            const ValueId output = nextValue();
            ir_.instructions.push_back(IrUnary{output, node.op, operand});
            return output;
        } else {
            const ValueId left = expression(*node.left);
            const ValueId right = expression(*node.right);
            const ValueId output = nextValue();
            ir_.instructions.push_back(IrBinary{output, node.op, left, right});
            return output;
        }
    }, expressionNode.value);
}

std::string IrGenerator::print(const IrProgram& program) {
    std::ostringstream out;
    out << "module {\n";
    for (std::size_t i = 0; i < program.slots.size(); ++i) {
        out << "  %" << program.slots[i].name << " = stack " << program.slots[i].type
            << " ; slot " << i << '\n';
    }
    for (const IrInstruction& instruction : program.instructions) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, IrConst>)
                out << "  $" << item.output << " = const.i32 " << item.value << '\n';
            else if constexpr (std::is_same_v<T, IrLoad>)
                out << "  $" << item.output << " = load.i32 %" << program.slots[item.slot].name << '\n';
            else if constexpr (std::is_same_v<T, IrUnary>)
                out << "  $" << item.output << " = " << (item.op == '-' ? "neg" : "copy")
                    << ".i32 $" << item.operand << '\n';
            else if constexpr (std::is_same_v<T, IrBinary>) {
                const char* name = item.op == '+' ? "add" : item.op == '-' ? "sub" :
                                   item.op == '*' ? "mul" : "div";
                out << "  $" << item.output << " = " << name << ".i32 $"
                    << item.left << ", $" << item.right << '\n';
            } else
                out << "  store.i32 $" << item.value << ", %" << program.slots[item.slot].name << '\n';
        }, instruction);
    }
    out << "}\n";
    return out.str();
}

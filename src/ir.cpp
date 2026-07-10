#include "ir.hpp"

#include "diagnostic.hpp"

#include <sstream>
#include <type_traits>

ValueId IrGenerator::nextValue() { return ir_.valueCount++; }

IrProgram IrGenerator::generate(const Program& program) {
    for (const Statement& statement : program.statements) {
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Declaration>) {
                if (symbols_.contains(node.name)) {
                    throw CompileError(node.location,
                                       "l'identifiant '" + node.name + "' est déjà défini");
                }
                // A declaration cannot refer to itself or to a later declaration.
                validateExpression(*node.initializer);
                if (node.kind == BindingKind::Def) {
                    symbols_.emplace(node.name,
                                     Symbol{0, node.kind, node.initializer.get()});
                } else {
                    const ValueId value = expression(*node.initializer);
                    const SlotId slot = ir_.slots.size();
                    symbols_.emplace(node.name, Symbol{slot, node.kind, nullptr});
                    ir_.slots.push_back(IrSlot{node.name, node.type});
                    ir_.instructions.push_back(IrStore{slot, value});
                }
            } else {
                const auto found = symbols_.find(node.name);
                if (found == symbols_.end()) {
                    throw CompileError(node.location,
                                       "identifiant inconnu '" + node.name + "'");
                }
                if (found->second.kind != BindingKind::Var) {
                    const std::string message = found->second.kind == BindingKind::Def
                        ? "la définition '" + node.name + "' est immuable"
                        : "la val '" + node.name + "' est immuable";
                    throw CompileError(node.location, message);
                }
                const ValueId value = expression(*node.value);
                ir_.instructions.push_back(IrStore{found->second.slot, value});
            }
        }, statement);
    }
    return std::move(ir_);
}

void IrGenerator::validateExpression(const Expression& expressionNode) const {
    std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NameExpr>) {
            if (!symbols_.contains(node.name)) {
                throw CompileError(expressionNode.location,
                                   "identifiant inconnu '" + node.name + "'");
            }
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            validateExpression(*node.operand);
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            validateExpression(*node.left);
            validateExpression(*node.right);
        }
    }, expressionNode.value);
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
            if (found->second.kind == BindingKind::Def) {
                return expression(*found->second.definition);
            }
            const ValueId output = nextValue();
            ir_.instructions.push_back(IrLoad{output, found->second.slot});
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

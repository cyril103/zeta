#include "ir.hpp"

#include "diagnostic.hpp"

#include <sstream>
#include <type_traits>
#include <unordered_set>

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
                std::unordered_set<std::string> parameters;
                for (const Parameter& parameter : node.parameters) {
                    if (!parameters.emplace(parameter.name).second) {
                        throw CompileError(parameter.location,
                                           "paramètre '" + parameter.name + "' déclaré plusieurs fois");
                    }
                }
                validateExpression(*node.initializer, parameters);
                if (node.kind == BindingKind::Def) {
                    symbols_.emplace(node.name,
                                     Symbol{0, node.kind, &node});
                } else {
                    const ValueId value = expression(*node.initializer);
                    const SlotId slot = ir_.slots.size();
                    symbols_.emplace(node.name, Symbol{slot, node.kind, &node});
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

void IrGenerator::validateExpression(
    const Expression& expressionNode,
    const std::unordered_set<std::string>& parameters) const {
    std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, NameExpr>) {
            if (!parameters.contains(node.name) && !symbols_.contains(node.name)) {
                throw CompileError(expressionNode.location,
                                   "identifiant inconnu '" + node.name + "'");
            }
            if (!parameters.contains(node.name)) {
                const Symbol& symbol = symbols_.at(node.name);
                if (symbol.kind == BindingKind::Def && symbol.declaration->callable) {
                    throw CompileError(expressionNode.location,
                                       "la fonction '" + node.name + "' doit être appelée");
                }
            }
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            const auto found = symbols_.find(node.name);
            if (found == symbols_.end()) {
                throw CompileError(expressionNode.location,
                                   "fonction inconnue '" + node.name + "'");
            }
            const Declaration* declaration = found->second.declaration;
            if (found->second.kind != BindingKind::Def || !declaration->callable) {
                throw CompileError(expressionNode.location,
                                   "'" + node.name + "' n'est pas une fonction");
            }
            if (node.arguments.size() != declaration->parameters.size()) {
                throw CompileError(expressionNode.location,
                                   "la fonction '" + node.name + "' attend " +
                                   std::to_string(declaration->parameters.size()) +
                                   " argument(s), reçu " + std::to_string(node.arguments.size()));
            }
            for (const ExprPtr& argument : node.arguments) {
                validateExpression(*argument, parameters);
            }
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            validateExpression(*node.operand, parameters);
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            validateExpression(*node.left, parameters);
            validateExpression(*node.right, parameters);
        }
    }, expressionNode.value);
}

ValueId IrGenerator::expression(const Expression& expressionNode) {
    static const std::unordered_map<std::string, ValueId> noParameters;
    return expression(expressionNode, noParameters);
}

ValueId IrGenerator::expression(
    const Expression& expressionNode,
    const std::unordered_map<std::string, ValueId>& parameters) {
    return std::visit([&](const auto& node) -> ValueId {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IntegerExpr>) {
            const ValueId output = nextValue();
            ir_.instructions.push_back(IrConst{output, node.value});
            return output;
        } else if constexpr (std::is_same_v<T, NameExpr>) {
            if (const auto parameter = parameters.find(node.name);
                parameter != parameters.end()) {
                return parameter->second;
            }
            const auto found = symbols_.find(node.name);
            if (found == symbols_.end()) {
                throw CompileError(expressionNode.location,
                                   "val inconnue '" + node.name + "'");
            }
            if (found->second.kind == BindingKind::Def) {
                if (found->second.declaration->callable) {
                    throw CompileError(expressionNode.location,
                                       "la fonction '" + node.name + "' doit être appelée");
                }
                return expression(*found->second.declaration->initializer, parameters);
            }
            const ValueId output = nextValue();
            ir_.instructions.push_back(IrLoad{output, found->second.slot});
            return output;
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            const auto found = symbols_.find(node.name);
            if (found == symbols_.end() || found->second.kind != BindingKind::Def ||
                !found->second.declaration->callable) {
                throw CompileError(expressionNode.location,
                                   "fonction invalide '" + node.name + "'");
            }
            const Declaration& function = *found->second.declaration;
            std::unordered_map<std::string, ValueId> arguments;
            for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                arguments.emplace(function.parameters[i].name,
                                  expression(*node.arguments[i], parameters));
            }
            return expression(*function.initializer, arguments);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            const ValueId operand = expression(*node.operand, parameters);
            const ValueId output = nextValue();
            ir_.instructions.push_back(IrUnary{output, node.op, operand});
            return output;
        } else {
            const ValueId left = expression(*node.left, parameters);
            const ValueId right = expression(*node.right, parameters);
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

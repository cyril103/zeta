#include "ir.hpp"

#include "diagnostic.hpp"

#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

ValueId IrGenerator::nextValue(ValueType type) {
    ir_.valueTypes.push_back(type);
    return ir_.valueCount++;
}

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
                std::unordered_map<std::string, ValueType> parameters;
                for (const Parameter& parameter : node.parameters) {
                    if (!parameters.emplace(parameter.name, parameter.type).second) {
                        throw CompileError(parameter.location,
                                           "paramètre '" + parameter.name + "' déclaré plusieurs fois");
                    }
                }
                validateExpression(*node.initializer, node.type, parameters);
                if (node.kind == BindingKind::Def) {
                    symbols_.emplace(node.name,
                                     Symbol{0, node.kind, &node});
                } else {
                    const ValueId value = expression(*node.initializer);
                    const SlotId slot = ir_.slots.size();
                    symbols_.emplace(node.name, Symbol{slot, node.kind, &node});
                    ir_.slots.push_back(IrSlot{node.name, node.type});
                    ir_.instructions.push_back(IrStore{slot, value, node.type});
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
                validateExpression(*node.value, found->second.declaration->type);
                const ValueId value = expression(*node.value);
                ir_.instructions.push_back(IrStore{found->second.slot, value,
                                                   found->second.declaration->type});
            }
        }, statement.value);
    }

    const auto mainSymbol = symbols_.find("main");
    if (mainSymbol == symbols_.end()) {
        throw CompileError(SourceLocation{1, 1},
                           "point d'entrée manquant : 'def main () : Int' est obligatoire");
    }
    const Declaration& mainDeclaration = *mainSymbol->second.declaration;
    if (mainDeclaration.kind != BindingKind::Def || !mainDeclaration.callable ||
        !mainDeclaration.parameters.empty() || mainDeclaration.type != ValueType::Int) {
        throw CompileError(mainDeclaration.location,
                           "le point d'entrée doit avoir la signature 'def main () : Int'");
    }
    ir_.exitValue = expression(*mainDeclaration.initializer);
    return std::move(ir_);
}

ValueType IrGenerator::validateExpression(
    const Expression& expressionNode,
    ValueType expected,
    const std::unordered_map<std::string, ValueType>& parameters) const {
    const std::unordered_map<std::string, const Declaration*> locals;
    return validateExpression(expressionNode, expected, parameters, locals);
}

ValueType IrGenerator::validateExpression(
    const Expression& expressionNode,
    ValueType expected,
    const std::unordered_map<std::string, ValueType>& parameters,
    const std::unordered_map<std::string, const Declaration*>& locals) const {
    const ValueType actual = std::visit([&](const auto& node) -> ValueType {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, IntegerExpr>) {
            if (expected == ValueType::Byte && node.value > 255) {
                throw CompileError(expressionNode.location,
                                   "le littéral " + std::to_string(node.value) +
                                   " dépasse l'intervalle Byte (0..255)");
            }
            return expected;
        } else if constexpr (std::is_same_v<T, DoubleExpr>) {
            return ValueType::Double;
        } else if constexpr (std::is_same_v<T, NameExpr>) {
            const auto local = locals.find(node.name);
            if (!parameters.contains(node.name) && local == locals.end() &&
                !symbols_.contains(node.name)) {
                throw CompileError(expressionNode.location,
                                   "identifiant inconnu '" + node.name + "'");
            }
            if (const auto parameter = parameters.find(node.name);
                parameter != parameters.end()) {
                return parameter->second;
            } else {
                const Declaration* declaration = local != locals.end()
                    ? local->second : symbols_.at(node.name).declaration;
                if (declaration->kind == BindingKind::Def && declaration->callable) {
                    throw CompileError(expressionNode.location,
                                       "la fonction '" + node.name + "' doit être appelée");
                }
                return declaration->type;
            }
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            const auto local = locals.find(node.name);
            const auto found = symbols_.find(node.name);
            if (local == locals.end() && found == symbols_.end()) {
                throw CompileError(expressionNode.location,
                                   "fonction inconnue '" + node.name + "'");
            }
            const Declaration* declaration = local != locals.end()
                ? local->second : found->second.declaration;
            if (declaration->kind != BindingKind::Def || !declaration->callable) {
                throw CompileError(expressionNode.location,
                                   "'" + node.name + "' n'est pas une fonction");
            }
            if (node.arguments.size() != declaration->parameters.size()) {
                throw CompileError(expressionNode.location,
                                   "la fonction '" + node.name + "' attend " +
                                   std::to_string(declaration->parameters.size()) +
                                   " argument(s), reçu " + std::to_string(node.arguments.size()));
            }
            for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                validateExpression(*node.arguments[i], declaration->parameters[i].type,
                                   parameters, locals);
            }
            return declaration->type;
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            validateExpression(*node.operand, expected, parameters, locals);
            return expected;
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            validateExpression(*node.left, expected, parameters, locals);
            validateExpression(*node.right, expected, parameters, locals);
            return expected;
        } else if constexpr (std::is_same_v<T, BlockExpr>) {
            auto blockLocals = locals;
            for (const StatementPtr& statement : node.statements) {
                std::visit([&](const auto& item) {
                    using S = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<S, Declaration>) {
                        if (parameters.contains(item.name) || blockLocals.contains(item.name) ||
                            symbols_.contains(item.name)) {
                            throw CompileError(item.location,
                                               "l'identifiant '" + item.name + "' est déjà défini");
                        }
                        std::unordered_map<std::string, ValueType> nestedParameters;
                        for (const Parameter& parameter : item.parameters) {
                            if (!nestedParameters.emplace(parameter.name, parameter.type).second) {
                                throw CompileError(parameter.location,
                                                   "paramètre '" + parameter.name +
                                                   "' déclaré plusieurs fois");
                            }
                        }
                        auto visibleParameters = parameters;
                        for (const auto& [name, type] : nestedParameters) {
                            visibleParameters.insert_or_assign(name, type);
                        }
                        validateExpression(*item.initializer, item.type,
                                           visibleParameters, blockLocals);
                        blockLocals.emplace(item.name, &item);
                    } else {
                        if (parameters.contains(item.name)) {
                            throw CompileError(item.location,
                                               "le paramètre '" + item.name + "' est immuable");
                        }
                        const auto localTarget = blockLocals.find(item.name);
                        const auto globalTarget = symbols_.find(item.name);
                        if (localTarget == blockLocals.end() && globalTarget == symbols_.end()) {
                            throw CompileError(item.location,
                                               "identifiant inconnu '" + item.name + "'");
                        }
                        const Declaration* declaration = localTarget != blockLocals.end()
                            ? localTarget->second : globalTarget->second.declaration;
                        if (declaration->kind != BindingKind::Var) {
                            throw CompileError(item.location,
                                               "l'identifiant '" + item.name + "' est immuable");
                        }
                        validateExpression(*item.value, declaration->type,
                                           parameters, blockLocals);
                    }
                }, statement->value);
            }
            validateExpression(*node.result, expected, parameters, blockLocals);
            return expected;
        }
    }, expressionNode.value);
    if (actual != expected) {
        throw CompileError(expressionNode.location,
                           "type " + typeName(expected) + " attendu, reçu " +
                           typeName(actual));
    }
    expressionNode.inferredType = actual;
    return actual;
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
            const ValueId output = nextValue(expressionNode.inferredType);
            if (expressionNode.inferredType == ValueType::Double)
                ir_.instructions.push_back(IrDoubleConst{output, static_cast<double>(node.value)});
            else
                ir_.instructions.push_back(IrConst{output, node.value,
                                                   expressionNode.inferredType});
            return output;
        } else if constexpr (std::is_same_v<T, DoubleExpr>) {
            const ValueId output = nextValue(ValueType::Double);
            ir_.instructions.push_back(IrDoubleConst{output, node.value});
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
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrLoad{output, found->second.slot,
                                              expressionNode.inferredType});
            return output;
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            const auto found = symbols_.find(node.name);
            if (found == symbols_.end() || found->second.kind != BindingKind::Def ||
                !found->second.declaration->callable) {
                throw CompileError(expressionNode.location,
                                   "fonction invalide '" + node.name + "'");
            }
            const Declaration& function = *found->second.declaration;
            std::unordered_map<std::string, ValueId> arguments = parameters;
            for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                arguments.insert_or_assign(function.parameters[i].name,
                                           expression(*node.arguments[i], parameters));
            }
            return expression(*function.initializer, arguments);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            const ValueId operand = expression(*node.operand, parameters);
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrUnary{output, node.op, operand,
                                               expressionNode.inferredType});
            return output;
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            const ValueId left = expression(*node.left, parameters);
            const ValueId right = expression(*node.right, parameters);
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrBinary{output, node.op, left, right,
                                                expressionNode.inferredType});
            return output;
        } else {
            std::vector<std::string> localNames;
            for (const StatementPtr& statement : node.statements) {
                std::visit([&](const auto& item) {
                    using S = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<S, Declaration>) {
                        localNames.push_back(item.name);
                        if (item.kind == BindingKind::Def) {
                            symbols_.emplace(item.name, Symbol{0, item.kind, &item});
                        } else {
                            const ValueId value = expression(*item.initializer, parameters);
                            const SlotId slot = ir_.slots.size();
                            symbols_.emplace(item.name, Symbol{slot, item.kind, &item});
                            ir_.slots.push_back(IrSlot{item.name, item.type});
                            ir_.instructions.push_back(IrStore{slot, value, item.type});
                        }
                    } else {
                        if (parameters.contains(item.name)) {
                            throw CompileError(item.location,
                                               "le paramètre '" + item.name + "' est immuable");
                        }
                        const auto found = symbols_.find(item.name);
                        if (found == symbols_.end() || found->second.kind != BindingKind::Var) {
                            throw CompileError(item.location,
                                               "affectation invalide de '" + item.name + "'");
                        }
                        const ValueId value = expression(*item.value, parameters);
                        ir_.instructions.push_back(IrStore{found->second.slot, value,
                                                          found->second.declaration->type});
                    }
                }, statement->value);
            }
            const ValueId result = expression(*node.result, parameters);
            for (auto name = localNames.rbegin(); name != localNames.rend(); ++name) {
                symbols_.erase(*name);
            }
            return result;
        }
    }, expressionNode.value);
}

std::string IrGenerator::print(const IrProgram& program) {
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    const auto suffix = [](ValueType type) {
        if (type == ValueType::Int) return "i32";
        if (type == ValueType::Byte) return "u8";
        return "f64";
    };
    out << "module {\n";
    for (std::size_t i = 0; i < program.slots.size(); ++i) {
        out << "  %" << program.slots[i].name << " = stack " << typeName(program.slots[i].type)
            << " ; slot " << i << '\n';
    }
    for (const IrInstruction& instruction : program.instructions) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, IrConst>)
                out << "  $" << item.output << " = const."
                    << (item.type == ValueType::Int ? "i32 " : "u8 ") << item.value << '\n';
            else if constexpr (std::is_same_v<T, IrDoubleConst>)
                out << "  $" << item.output << " = const.f64 " << item.value << '\n';
            else if constexpr (std::is_same_v<T, IrLoad>)
                out << "  $" << item.output << " = load."
                    << suffix(item.type) << " %" << program.slots[item.slot].name << '\n';
            else if constexpr (std::is_same_v<T, IrUnary>)
                out << "  $" << item.output << " = " << (item.op == '-' ? "neg" : "copy")
                    << '.' << suffix(item.type) << " $" << item.operand << '\n';
            else if constexpr (std::is_same_v<T, IrBinary>) {
                const char* name = item.op == '+' ? "add" : item.op == '-' ? "sub" :
                                   item.op == '*' ? "mul" : "div";
                out << "  $" << item.output << " = " << name
                    << '.' << suffix(item.type) << " $"
                    << item.left << ", $" << item.right << '\n';
            } else
                out << "  store." << suffix(item.type) << " $" << item.value
                    << ", %" << program.slots[item.slot].name << '\n';
        }, instruction);
    }
    out << "  exit.i32 $" << program.exitValue << '\n';
    out << "}\n";
    return out.str();
}

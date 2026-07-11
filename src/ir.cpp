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

IrProgram IrGenerator::generate(const TypedProgram& typedProgram) {
    const Program& program = typedProgram.ast();
    ir_ = IrProgram{};
    symbols_.clear();
    nextLabel_ = 0;
    loopLabels_.clear();
    inFunction_ = false;
    std::vector<const Declaration*> functions;
    for (const Statement& statement : program.statements) {
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Declaration>) {
                if (node.kind == BindingKind::Def) {
                    symbols_.emplace(node.name,
                                     Symbol{0, node.kind, &node, true});
                    if (node.callable) functions.push_back(&node);
                } else {
                    const ValueId value = expression(*node.initializer);
                    const SlotId slot = ir_.slots.size();
                    symbols_.emplace(node.name, Symbol{slot, node.kind, &node, true});
                    ir_.slots.push_back(IrSlot{node.name, node.type, true});
                    ir_.instructions.push_back(IrStore{slot, value, node.type});
                }
            } else if constexpr (std::is_same_v<T, Assignment>) {
                const auto found = symbols_.find(node.name);
                const ValueId value = expression(*node.value);
                ir_.instructions.push_back(IrStore{found->second.slot, value,
                                                   found->second.declaration->type});
            } else if constexpr (std::is_same_v<T, WhileStatement>) {
                const std::unordered_map<std::string, ValueId> parameterValues;
                emitLoop(node, parameterValues);
            } else {
                throw CompileError(node.location,
                                   "une expression seule n'est pas autorisée au niveau global");
            }
        }, statement.value);
    }

    const ValueId mainResult = nextValue(ValueType::Int);
    ir_.instructions.push_back(IrCall{mainResult, "main", {}, {}, ValueType::Int});
    ir_.instructions.push_back(IrExit{mainResult});
    ir_.exitValue = mainResult;

    inFunction_ = true;
    for (const Declaration* function : functions) {
        ir_.instructions.push_back(IrFunctionStart{function->name});
        std::unordered_map<std::string, ValueId> parameters;
        for (std::size_t i = 0; i < function->parameters.size(); ++i) {
            const Parameter& parameter = function->parameters[i];
            const ValueId value = nextValue(parameter.type);
            parameters.emplace(parameter.name, value);
            ir_.instructions.push_back(IrParameter{value, i, parameter.type});
        }
        emitTailExpression(*function->initializer, parameters, *function);
    }
    return std::move(ir_);
}

void IrGenerator::emitTailExpression(
    const Expression& expressionNode,
    const std::unordered_map<std::string, ValueId>& parameters,
    const Declaration& function) {
    if (const auto* block = std::get_if<BlockExpr>(&expressionNode.value)) {
        std::vector<std::string> localNames;
        for (const StatementPtr& statement : block->statements) {
            std::visit([&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Declaration>) {
                    localNames.push_back(item.name);
                    if (item.kind == BindingKind::Def) {
                        symbols_.emplace(item.name, Symbol{0, item.kind, &item, false});
                    } else {
                        const ValueId value = expression(*item.initializer, parameters);
                        const SlotId slot = ir_.slots.size();
                        symbols_.emplace(item.name, Symbol{slot, item.kind, &item, false});
                        ir_.slots.push_back(IrSlot{item.name, item.type, false});
                        ir_.instructions.push_back(IrStore{slot, value, item.type});
                    }
                } else if constexpr (std::is_same_v<T, Assignment>) {
                    const auto found = symbols_.find(item.name);
                    const ValueId value = expression(*item.value, parameters);
                    ir_.instructions.push_back(IrStore{found->second.slot, value,
                                                       found->second.declaration->type});
                } else if constexpr (std::is_same_v<T, WhileStatement>) {
                    emitLoop(item, parameters);
                } else if constexpr (std::is_same_v<T, ExpressionStatement>) {
                    expression(*item.expression, parameters);
                } else if constexpr (std::is_same_v<T, ReturnStatement>) {
                    const ValueId value = expression(*item.value, parameters);
                    ir_.instructions.push_back(IrReturn{value, item.value->inferredType});
                } else if constexpr (std::is_same_v<T, BreakStatement>) {
                    ir_.instructions.push_back(IrJump{loopLabels_.back().second});
                } else {
                    ir_.instructions.push_back(IrJump{loopLabels_.back().first});
                }
            }, statement->value);
        }
        if (block->result) emitTailExpression(*block->result, parameters, function);
        for (auto name = localNames.rbegin(); name != localNames.rend(); ++name)
            symbols_.erase(*name);
        return;
    }
    if (const auto* call = std::get_if<CallExpr>(&expressionNode.value);
        call != nullptr && call->name == function.name) {
        std::vector<ValueId> arguments;
        std::vector<ValueType> argumentTypes;
        for (std::size_t i = 0; i < call->arguments.size(); ++i) {
            arguments.push_back(expression(*call->arguments[i], parameters));
            argumentTypes.push_back(function.parameters[i].type);
        }
        ir_.instructions.push_back(IrTailCall{function.name, std::move(arguments),
                                              std::move(argumentTypes)});
        return;
    }
    if (const auto* conditional = std::get_if<IfExpr>(&expressionNode.value)) {
        const ValueId condition = expression(*conditional->condition, parameters);
        const std::size_t elseLabel = nextLabel_++;
        ir_.instructions.push_back(IrBranch{condition, false, elseLabel});
        emitTailExpression(*conditional->thenBranch, parameters, function);
        ir_.instructions.push_back(IrLabel{elseLabel});
        emitTailExpression(*conditional->elseBranch, parameters, function);
        return;
    }
    const ValueId result = expression(expressionNode, parameters);
    ir_.instructions.push_back(IrReturn{result, function.type});
}


void IrGenerator::emitLoop(
    const WhileStatement& loop,
    const std::unordered_map<std::string, ValueId>& parameters) {
    const std::size_t conditionLabel = nextLabel_++;
    const std::size_t endLabel = nextLabel_++;
    ir_.instructions.push_back(IrLabel{conditionLabel});
    const ValueId condition = expression(*loop.condition, parameters);
    ir_.instructions.push_back(IrBranch{condition, false, endLabel});
    loopLabels_.emplace_back(conditionLabel, endLabel);

    std::vector<std::string> localNames;
    for (const StatementPtr& statement : loop.body) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, Declaration>) {
                localNames.push_back(item.name);
                if (item.kind == BindingKind::Def) {
                    symbols_.emplace(item.name, Symbol{0, item.kind, &item, false});
                } else {
                    const ValueId value = expression(*item.initializer, parameters);
                    const SlotId slot = ir_.slots.size();
                    symbols_.emplace(item.name, Symbol{slot, item.kind, &item, false});
                    ir_.slots.push_back(IrSlot{item.name, item.type, false});
                    ir_.instructions.push_back(IrStore{slot, value, item.type});
                }
            } else if constexpr (std::is_same_v<T, Assignment>) {
                const auto found = symbols_.find(item.name);
                const ValueId value = expression(*item.value, parameters);
                ir_.instructions.push_back(IrStore{found->second.slot, value,
                                                   found->second.declaration->type});
            } else if constexpr (std::is_same_v<T, WhileStatement>) {
                emitLoop(item, parameters);
            } else if constexpr (std::is_same_v<T, ExpressionStatement>) {
                expression(*item.expression, parameters);
            } else if constexpr (std::is_same_v<T, ReturnStatement>) {
                const ValueId value = expression(*item.value, parameters);
                ir_.instructions.push_back(IrReturn{value, item.value->inferredType});
            } else if constexpr (std::is_same_v<T, BreakStatement>) {
                ir_.instructions.push_back(IrJump{endLabel});
            } else {
                ir_.instructions.push_back(IrJump{conditionLabel});
            }
        }, statement->value);
    }
    ir_.instructions.push_back(IrJump{conditionLabel});
    ir_.instructions.push_back(IrLabel{endLabel});
    loopLabels_.pop_back();
    for (auto name = localNames.rbegin(); name != localNames.rend(); ++name)
        symbols_.erase(*name);
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
        } else if constexpr (std::is_same_v<T, BoolExpr>) {
            const ValueId output = nextValue(ValueType::Bool);
            ir_.instructions.push_back(IrConst{output, node.value ? 1 : 0, ValueType::Bool});
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
            if (!found->second.global) {
                std::unordered_map<std::string, ValueId> localArguments = parameters;
                for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                    localArguments.insert_or_assign(function.parameters[i].name,
                        expression(*node.arguments[i], parameters));
                }
                return expression(*function.initializer, localArguments);
            }
            std::vector<ValueId> arguments;
            std::vector<ValueType> argumentTypes;
            for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                arguments.push_back(expression(*node.arguments[i], parameters));
                argumentTypes.push_back(function.parameters[i].type);
            }
            const ValueId output = nextValue(function.type);
            ir_.instructions.push_back(IrCall{output, node.name, std::move(arguments),
                                              std::move(argumentTypes), function.type});
            return output;
        } else if constexpr (std::is_same_v<T, ConversionExpr>) {
            const ValueId input = expression(*node.operand, parameters);
            const ValueId output = nextValue(node.target);
            ir_.instructions.push_back(IrConvert{output, input,
                                                  node.operand->inferredType, node.target});
            return output;
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            const ValueId operand = expression(*node.operand, parameters);
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrUnary{output, node.op, operand,
                                               expressionNode.inferredType});
            return output;
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            if (node.op == "&&" || node.op == "||") {
                const ValueId left = expression(*node.left, parameters);
                const ValueId output = nextValue(ValueType::Bool);
                const std::size_t endLabel = nextLabel_++;
                ir_.instructions.push_back(IrCopy{output, left, ValueType::Bool});
                ir_.instructions.push_back(IrBranch{left, node.op == "||", endLabel});
                const ValueId right = expression(*node.right, parameters);
                ir_.instructions.push_back(IrCopy{output, right, ValueType::Bool});
                ir_.instructions.push_back(IrLabel{endLabel});
                return output;
            }
            const ValueId left = expression(*node.left, parameters);
            const ValueId right = expression(*node.right, parameters);
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrBinary{output, node.op, left, right,
                                                expressionNode.inferredType,
                                                node.left->inferredType});
            return output;
        } else if constexpr (std::is_same_v<T, BlockExpr>) {
            std::vector<std::string> localNames;
            for (const StatementPtr& statement : node.statements) {
                std::visit([&](const auto& item) {
                    using S = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<S, Declaration>) {
                        localNames.push_back(item.name);
                        if (item.kind == BindingKind::Def) {
                            symbols_.emplace(item.name, Symbol{0, item.kind, &item, false});
                        } else {
                            const ValueId value = expression(*item.initializer, parameters);
                            const SlotId slot = ir_.slots.size();
                            symbols_.emplace(item.name, Symbol{slot, item.kind, &item, false});
                            ir_.slots.push_back(IrSlot{item.name, item.type, false});
                            ir_.instructions.push_back(IrStore{slot, value, item.type});
                        }
                    } else if constexpr (std::is_same_v<S, Assignment>) {
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
                    } else if constexpr (std::is_same_v<S, WhileStatement>) {
                        emitLoop(item, parameters);
                    } else if constexpr (std::is_same_v<S, ExpressionStatement>) {
                        expression(*item.expression, parameters);
                    } else if constexpr (std::is_same_v<S, ReturnStatement>) {
                        const ValueId value = expression(*item.value, parameters);
                        ir_.instructions.push_back(IrReturn{value, item.value->inferredType});
                    } else if constexpr (std::is_same_v<S, BreakStatement>) {
                        ir_.instructions.push_back(IrJump{loopLabels_.back().second});
                    } else {
                        ir_.instructions.push_back(IrJump{loopLabels_.back().first});
                    }
                }, statement->value);
            }
            const ValueId result = node.result ? expression(*node.result, parameters)
                                               : nextValue(expressionNode.inferredType);
            for (auto name = localNames.rbegin(); name != localNames.rend(); ++name) {
                symbols_.erase(*name);
            }
            return result;
        } else {
            const ValueId condition = expression(*node.condition, parameters);
            const ValueId output = nextValue(expressionNode.inferredType);
            const std::size_t elseLabel = nextLabel_++;
            const std::size_t endLabel = nextLabel_++;
            ir_.instructions.push_back(IrBranch{condition, false, elseLabel});
            const ValueId thenValue = expression(*node.thenBranch, parameters);
            ir_.instructions.push_back(IrCopy{output, thenValue, expressionNode.inferredType});
            ir_.instructions.push_back(IrJump{endLabel});
            ir_.instructions.push_back(IrLabel{elseLabel});
            const ValueId elseValue = expression(*node.elseBranch, parameters);
            ir_.instructions.push_back(IrCopy{output, elseValue, expressionNode.inferredType});
            ir_.instructions.push_back(IrLabel{endLabel});
            return output;
        }
    }, expressionNode.value);
}

std::string IrGenerator::print(const IrProgram& program) {
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    const auto suffix = [](ValueType type) {
        if (type == ValueType::Int) return "i32";
        if (type == ValueType::Byte) return "u8";
        if (type == ValueType::Double) return "f64";
        return "i1";
    };
    out << "module {\n";
    for (std::size_t i = 0; i < program.slots.size(); ++i) {
        out << "  %" << program.slots[i].name << " = "
            << (program.slots[i].global ? "global " : "stack ") << typeName(program.slots[i].type)
            << " ; slot " << i << '\n';
    }
    for (const IrInstruction& instruction : program.instructions) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, IrConst>)
                out << "  $" << item.output << " = const." << suffix(item.type)
                    << ' ' << item.value << '\n';
            else if constexpr (std::is_same_v<T, IrDoubleConst>)
                out << "  $" << item.output << " = const.f64 " << item.value << '\n';
            else if constexpr (std::is_same_v<T, IrLoad>)
                out << "  $" << item.output << " = load."
                    << suffix(item.type) << " %" << program.slots[item.slot].name << '\n';
            else if constexpr (std::is_same_v<T, IrConvert>)
                out << "  $" << item.output << " = convert."
                    << suffix(item.source) << ".to." << suffix(item.target)
                    << " $" << item.input << '\n';
            else if constexpr (std::is_same_v<T, IrUnary>)
                out << "  $" << item.output << " = "
                    << (item.op == "-" ? "neg" : item.op == "!" ? "not" : "copy")
                    << '.' << suffix(item.type) << " $" << item.operand << '\n';
            else if constexpr (std::is_same_v<T, IrBinary>) {
                const std::string name = item.op == "+" ? "add" : item.op == "-" ? "sub" :
                    item.op == "*" ? "mul" : item.op == "/" ? "div" :
                    item.op == "==" ? "eq" : item.op == "!=" ? "ne" :
                    item.op == "<" ? "lt" : item.op == "<=" ? "le" :
                    item.op == ">" ? "gt" : item.op == ">=" ? "ge" :
                    item.op == "&&" ? "and" : "or";
                out << "  $" << item.output << " = " << name
                    << '.' << suffix(item.operandType) << " $"
                    << item.left << ", $" << item.right << '\n';
            } else
                if constexpr (std::is_same_v<T, IrStore>)
                    out << "  store." << suffix(item.type) << " $" << item.value
                        << ", %" << program.slots[item.slot].name << '\n';
                else if constexpr (std::is_same_v<T, IrCopy>)
                    out << "  $" << item.output << " = copy." << suffix(item.type)
                        << " $" << item.input << '\n';
                else if constexpr (std::is_same_v<T, IrCall>) {
                    out << "  $" << item.output << " = call " << item.function << '(';
                    for (std::size_t i = 0; i < item.arguments.size(); ++i) {
                        if (i != 0) out << ", ";
                        out << '$' << item.arguments[i];
                    }
                    out << ") : " << suffix(item.returnType) << '\n';
                } else if constexpr (std::is_same_v<T, IrTailCall>) {
                    out << "  tail_call " << item.function << '(';
                    for (std::size_t i = 0; i < item.arguments.size(); ++i) {
                        if (i != 0) out << ", ";
                        out << '$' << item.arguments[i];
                    }
                    out << ")\n";
                } else if constexpr (std::is_same_v<T, IrFunctionStart>)
                    out << "\n  function " << item.name << ":\n";
                else if constexpr (std::is_same_v<T, IrParameter>)
                    out << "  $" << item.output << " = parameter."
                        << suffix(item.type) << ' ' << item.index << '\n';
                else if constexpr (std::is_same_v<T, IrReturn>)
                    out << "  return." << suffix(item.type) << " $" << item.value << '\n';
                else if constexpr (std::is_same_v<T, IrExit>)
                    out << "  exit.i32 $" << item.value << '\n';
                else if constexpr (std::is_same_v<T, IrBranch>)
                    out << "  branch." << (item.jumpWhenTrue ? "true" : "false")
                        << " $" << item.condition << ", label" << item.label << '\n';
                else if constexpr (std::is_same_v<T, IrJump>)
                    out << "  jump label" << item.label << '\n';
                else
                    out << "label" << item.label << ":\n";
        }, instruction);
    }
    out << "}\n";
    return out.str();
}

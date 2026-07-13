#include "ir.hpp"

#include "diagnostic.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

ValueId IrGenerator::nextValue(ValueType type) {
    ir_.valueTypes.push_back(resolveType(type));
    return ir_.valueCount++;
}

ValueType IrGenerator::resolveType(const ValueType& type) const {
    if (type.kind == ValueType::Kind::TypeParameter) return typeSubstitutions_.at(type.typeParameter);
    if (type.kind == ValueType::Kind::Array)
        return ValueType(std::make_shared<ValueType>(resolveType(*type.element)), type.length);
    if (type.kind == ValueType::Kind::Reference)
        return ValueType(std::make_shared<ValueType>(resolveType(*type.element)),
                         type.mutableReference);
    if (type.kind == ValueType::Kind::Slice)
        return ValueType(ValueType::Kind::Slice,
                         std::make_shared<ValueType>(resolveType(*type.element)),
                         type.mutableReference);
    if (type.kind == ValueType::Kind::Box)
        return ValueType(ValueType::Kind::Box,
                         std::make_shared<ValueType>(resolveType(*type.element)));
    if (type.kind == ValueType::Kind::Enum &&
        !type.enumeration->typeArguments.empty()) {
        std::vector<ValueType> arguments;
        for (const ValueType& argument : type.enumeration->typeArguments)
            arguments.push_back(resolveType(argument));
        return ValueType(instantiateEnumType(type.enumeration, std::move(arguments),
                                             type.enumeration->location));
    }
    return type;
}

std::string IrGenerator::genericLinkName(
    const Declaration& declaration, const std::vector<ValueType>& types) const {
    std::string name = declaration.name;
    for (const ValueType& type : types) {
        name += "__";
        for (char character : typeName(type))
            name += std::isalnum(static_cast<unsigned char>(character)) ? character : '_';
    }
    return name;
}

void IrGenerator::registerGenericInstance(
    const Declaration& declaration, const std::vector<ValueType>& types) {
    const std::string linkName = genericLinkName(declaration, types);
    if (genericInstanceNames_.insert(linkName).second)
        genericInstances_.push_back(GenericInstance{&declaration, types, linkName});
}

IrProgram IrGenerator::generate(const TypedProgram& typedProgram) {
    const Program& program = typedProgram.ast();
    ir_ = IrProgram{};
    symbols_.clear();
    movedBoxes_.clear();
    boxScopes_.clear();
    boxParameters_.clear();
    typeSubstitutions_.clear();
    genericInstances_.clear();
    genericInstanceNames_.clear();
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
                                     Symbol{0, node.kind, &node, true, node.name});
                    if (node.callable && !node.nativeSymbol && node.typeParameters.empty())
                        functions.push_back(&node);
                } else {
                    const ValueId value = expression(*node.initializer);
                    const SlotId slot = ir_.slots.size();
                    symbols_.emplace(node.name, Symbol{slot, node.kind, &node, true, node.name});
                    ir_.slots.push_back(IrSlot{node.name, node.type, true});
                    ir_.instructions.push_back(IrStore{slot, value, node.type});
                }
            } else if constexpr (std::is_same_v<T, Assignment>) {
                const auto found = symbols_.find(node.name);
                const ValueId value = expression(*node.value);
                ir_.instructions.push_back(IrStore{found->second.slot, value,
                                                   found->second.declaration->type});
            } else if constexpr (std::is_same_v<T, IndexAssignment>) {
                emitIndexStore(node, {});
            } else if constexpr (std::is_same_v<T, FieldAssignment>) {
                emitFieldStore(node, {});
            } else if constexpr (std::is_same_v<T, DereferenceAssignment>) {
                const ValueId reference = expression(*node.reference);
                const ValueId value = expression(*node.value);
                ir_.instructions.push_back(IrDereferenceStore{
                    reference, value, node.value->inferredType});
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
        boxParameters_.clear();
        movedBoxes_.clear();
        ir_.instructions.push_back(IrFunctionStart{function->name});
        std::unordered_map<std::string, ValueId> parameters;
        std::size_t stackOffset = 16U;
        for (std::size_t i = 0; i < function->parameters.size(); ++i) {
            const Parameter& parameter = function->parameters[i];
            const ValueId value = nextValue(parameter.type);
            parameters.emplace(parameter.name, value);
            if (isMoveOnlyValueType(parameter.type))
                boxParameters_.insert_or_assign(parameter.name,
                                                 std::pair{value, parameter.type});
            ir_.instructions.push_back(IrParameter{value, i, stackOffset, parameter.type});
            stackOffset += (parameter.type.kind == ValueType::Kind::Struct ||
                            parameter.type.kind == ValueType::Kind::Enum)
                ? (valueTypeSize(parameter.type) + 7U) / 8U * 8U
                : parameter.type == ValueType::String || parameter.type.kind == ValueType::Kind::Slice ? 16U : 8U;
        }
        emitTailExpression(*function->initializer, parameters, *function);
    }
    for (std::size_t instanceIndex = 0; instanceIndex < genericInstances_.size(); ++instanceIndex) {
        const GenericInstance instance = genericInstances_[instanceIndex];
        const Declaration& function = *instance.declaration;
        typeSubstitutions_.clear();
        for (std::size_t i = 0; i < function.typeParameters.size(); ++i)
            typeSubstitutions_.emplace(function.typeParameters[i], instance.types[i]);
        boxParameters_.clear();
        movedBoxes_.clear();
        ir_.instructions.push_back(IrFunctionStart{instance.linkName});
        std::unordered_map<std::string, ValueId> parameters;
        std::size_t stackOffset = 16U;
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            const Parameter& parameter = function.parameters[i];
            const ValueType parameterType = resolveType(parameter.type);
            const ValueId value = nextValue(parameterType);
            parameters.emplace(parameter.name, value);
            if (isMoveOnlyValueType(parameterType))
                boxParameters_.insert_or_assign(parameter.name,
                                                 std::pair{value, parameterType});
            ir_.instructions.push_back(IrParameter{value, i, stackOffset, parameterType});
            stackOffset += (parameterType.kind == ValueType::Kind::Struct ||
                            parameterType.kind == ValueType::Kind::Enum)
                ? (valueTypeSize(parameterType) + 7U) / 8U * 8U
                : parameterType == ValueType::String || parameterType.kind == ValueType::Kind::Slice ? 16U : 8U;
        }
        emitTailExpression(*function.initializer, parameters, function);
    }
    typeSubstitutions_.clear();
    return std::move(ir_);
}

IrProgram IrGenerator::generate(const ModuleGraph& graph) {
    moduleFilter_.reset();
    emitEntryPoint_ = true;
    return generateModule(graph, {});
}

IrProgram IrGenerator::generateModule(const ModuleGraph& graph, const std::string& moduleFilter) {
    if (!moduleFilter.empty()) {
        moduleFilter_ = moduleFilter;
        emitEntryPoint_ = false;
    }
    ir_ = IrProgram{};
    symbols_.clear();
    movedBoxes_.clear();
    boxScopes_.clear();
    boxParameters_.clear();
    typeSubstitutions_.clear();
    genericInstances_.clear();
    genericInstanceNames_.clear();
    nextLabel_ = 0;
    loopLabels_.clear();
    inFunction_ = false;
    struct FunctionRecord {
        std::string module;
        const Declaration* declaration;
        std::string linkName;
    };
    std::vector<FunctionRecord> functions;

    for (const auto& [moduleName, module] : graph.modules) {
        for (const Statement& statement : module.program.statements) {
            const auto* declaration = std::get_if<Declaration>(&statement.value);
            if (declaration == nullptr || declaration->kind != BindingKind::Def) continue;
            const std::string canonical = moduleName + "." + declaration->name;
            const std::string linkName = moduleName + "__" + declaration->name;
            symbols_.emplace(canonical,
                Symbol{0, declaration->kind, declaration, true, linkName});
            if ((!moduleFilter_ || *moduleFilter_ == moduleName) &&
                declaration->callable && !declaration->nativeSymbol &&
                declaration->typeParameters.empty())
                functions.push_back(FunctionRecord{moduleName, declaration, linkName});
        }
    }

    {
        for (const auto& [moduleName, module] : graph.modules) {
            for (const Statement& statement : module.program.statements) {
                const auto* declaration = std::get_if<Declaration>(&statement.value);
                if (declaration == nullptr || declaration->kind == BindingKind::Def) continue;
                const SlotId slot = ir_.slots.size();
                const std::string canonical = moduleName + "." + declaration->name;
                symbols_.insert_or_assign(canonical, Symbol{slot, declaration->kind,
                    declaration, true, moduleName + "__" + declaration->name});
                ir_.slots.push_back(IrSlot{moduleName + "__" + declaration->name,
                    declaration->type, true,
                    moduleFilter_.has_value() && moduleName != *moduleFilter_});
            }
        }
    }

    for (const std::string& moduleName : graph.compilationOrder) {
        if (moduleFilter_ && *moduleFilter_ != moduleName) continue;
        const Module& module = graph.modules.at(moduleName);
        if (module.precompiled) continue;
        std::vector<std::string> aliases;
        for (const Statement& statement : module.program.statements) {
            if (const auto* declaration = std::get_if<Declaration>(&statement.value);
                declaration != nullptr && declaration->kind == BindingKind::Def) {
                aliases.push_back(declaration->name);
                symbols_.insert_or_assign(declaration->name,
                                          symbols_.at(moduleName + "." + declaration->name));
            }
        }
        for (const Statement& statement : module.program.statements) {
            std::visit([&](const auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Declaration>) {
                    if (node.kind == BindingKind::Def) return;
                    const ValueId value = expression(*node.initializer);
                    const std::string canonical = moduleName + "." + node.name;
                    const Symbol symbol = symbols_.at(canonical);
                    const SlotId slot = symbol.slot;
                    symbols_.insert_or_assign(node.name, symbol);
                    aliases.push_back(node.name);
                    ir_.instructions.push_back(IrStore{slot, value, node.type});
                } else if constexpr (std::is_same_v<T, Assignment>) {
                    const auto found = symbols_.find(node.name);
                    const ValueId value = expression(*node.value);
                    ir_.instructions.push_back(IrStore{found->second.slot, value,
                                                       found->second.declaration->type});
                } else if constexpr (std::is_same_v<T, IndexAssignment>) {
                    emitIndexStore(node, {});
                } else if constexpr (std::is_same_v<T, FieldAssignment>) {
                    emitFieldStore(node, {});
                } else if constexpr (std::is_same_v<T, DereferenceAssignment>) {
                    const ValueId reference = expression(*node.reference);
                    const ValueId value = expression(*node.value);
                    ir_.instructions.push_back(IrDereferenceStore{
                        reference, value, node.value->inferredType});
                } else if constexpr (std::is_same_v<T, WhileStatement>) {
                    const std::unordered_map<std::string, ValueId> parameterValues;
                    emitLoop(node, parameterValues);
                }
            }, statement.value);
        }
        for (const std::string& alias : aliases) symbols_.erase(alias);
    }

    if (emitEntryPoint_) {
        const ValueId mainResult = nextValue(ValueType::Int);
        ir_.instructions.push_back(IrCall{mainResult, graph.root + "__main", {}, {}, ValueType::Int});
        ir_.instructions.push_back(IrExit{mainResult});
        ir_.exitValue = mainResult;
    }
    inFunction_ = true;

    for (const FunctionRecord& record : functions) {
        boxParameters_.clear();
        movedBoxes_.clear();
        const Module& module = graph.modules.at(record.module);
        std::vector<std::string> aliases;
        for (const Statement& statement : module.program.statements) {
            const auto* declaration = std::get_if<Declaration>(&statement.value);
            if (declaration == nullptr) continue;
            const auto canonical = symbols_.find(record.module + "." + declaration->name);
            if (canonical != symbols_.end()) {
                symbols_.insert_or_assign(declaration->name, canonical->second);
                aliases.push_back(declaration->name);
            }
        }
        const Declaration* function = record.declaration;
        ir_.instructions.push_back(IrFunctionStart{record.linkName});
        std::unordered_map<std::string, ValueId> parameters;
        std::size_t stackOffset = 16U;
        for (std::size_t i = 0; i < function->parameters.size(); ++i) {
            const Parameter& parameter = function->parameters[i];
            const ValueId value = nextValue(parameter.type);
            parameters.emplace(parameter.name, value);
            if (isMoveOnlyValueType(parameter.type))
                boxParameters_.insert_or_assign(parameter.name,
                                                 std::pair{value, parameter.type});
            ir_.instructions.push_back(IrParameter{value, i, stackOffset, parameter.type});
            stackOffset += (parameter.type.kind == ValueType::Kind::Struct ||
                            parameter.type.kind == ValueType::Kind::Enum)
                ? (valueTypeSize(parameter.type) + 7U) / 8U * 8U
                : parameter.type == ValueType::String || parameter.type.kind == ValueType::Kind::Slice ? 16U : 8U;
        }
        emitTailExpression(*function->initializer, parameters, *function);
        for (const std::string& alias : aliases) symbols_.erase(alias);
    }
    for (std::size_t instanceIndex = 0; instanceIndex < genericInstances_.size(); ++instanceIndex) {
        const GenericInstance instance = genericInstances_[instanceIndex];
        const Declaration& function = *instance.declaration;
        typeSubstitutions_.clear();
        for (std::size_t i = 0; i < function.typeParameters.size(); ++i)
            typeSubstitutions_.emplace(function.typeParameters[i], instance.types[i]);
        boxParameters_.clear();
        movedBoxes_.clear();
        ir_.instructions.push_back(IrFunctionStart{instance.linkName});
        std::unordered_map<std::string, ValueId> parameters;
        std::size_t stackOffset = 16U;
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            const ValueType parameterType = resolveType(function.parameters[i].type);
            const ValueId value = nextValue(parameterType);
            parameters.emplace(function.parameters[i].name, value);
            if (isMoveOnlyValueType(parameterType))
                boxParameters_.insert_or_assign(function.parameters[i].name,
                                                 std::pair{value, parameterType});
            ir_.instructions.push_back(IrParameter{value, i, stackOffset, parameterType});
            stackOffset += (parameterType.kind == ValueType::Kind::Struct ||
                            parameterType.kind == ValueType::Kind::Enum)
                ? (valueTypeSize(parameterType) + 7U) / 8U * 8U
                : parameterType == ValueType::String || parameterType.kind == ValueType::Kind::Slice ? 16U : 8U;
        }
        emitTailExpression(*function.initializer, parameters, function);
    }
    typeSubstitutions_.clear();
    return std::move(ir_);
}

void IrGenerator::emitIndexStore(
    const IndexAssignment& assignment,
    const std::unordered_map<std::string, ValueId>& parameters) {
    const auto parameter = parameters.find(assignment.name);
    const auto symbol = symbols_.find(assignment.name);
    const bool isParameter = parameter != parameters.end();
    const ValueType targetType = isParameter
        ? ir_.valueTypes[parameter->second]
        : symbol->second.declaration->type;
    const bool throughReference = targetType.kind == ValueType::Kind::Reference;
    const bool throughSlice = targetType.kind == ValueType::Kind::Slice;

    ValueId array = 0;
    SlotId slot = 0;
    if (throughReference || throughSlice) {
        if (isParameter) {
            array = parameter->second;
        } else {
            array = nextValue(targetType);
            ir_.instructions.push_back(IrLoad{array, symbol->second.slot, targetType});
        }
    } else {
        slot = symbol->second.slot;
    }

    std::vector<ValueId> indexes;
    for (const ExprPtr& index : assignment.indexes)
        indexes.push_back(expression(*index, parameters));
    const ValueId value = expression(*assignment.value, parameters);
    const ValueType arrayType = throughReference ? *targetType.element : targetType;
    ir_.instructions.push_back(IrIndexStore{slot, array, std::move(indexes), value,
                                            arrayType, throughReference, throughSlice});
}

void IrGenerator::emitFieldStore(
    const FieldAssignment& assignment,
    const std::unordered_map<std::string, ValueId>& parameters) {
    const auto symbol = symbols_.find(assignment.name);
    const ValueType type = symbol->second.declaration->type;
    const auto& fields = type.structure->fields;
    const auto field = std::find_if(fields.begin(), fields.end(), [&](const StructField& candidate) {
        return candidate.name == assignment.field;
    });
    ir_.instructions.push_back(IrFieldStore{symbol->second.slot,
        expression(*assignment.value, parameters), type,
        static_cast<std::size_t>(field - fields.begin())});
}

void IrGenerator::emitBoxDrops(const std::vector<std::string>& names) {
    for (auto name = names.rbegin(); name != names.rend(); ++name) {
        const auto symbol = symbols_.find(*name);
        if (symbol == symbols_.end() ||
            !valueTypeNeedsDrop(symbol->second.declaration->type) ||
            movedBoxes_.contains(*name))
            continue;
        const ValueType type = symbol->second.declaration->type;
        const ValueId value = nextValue(type);
        ir_.instructions.push_back(IrLoad{value, symbol->second.slot, type});
        ir_.instructions.push_back(IrDrop{value, type});
    }
}

void IrGenerator::emitAllBoxDrops() {
    for (auto scope = boxScopes_.rbegin(); scope != boxScopes_.rend(); ++scope)
        emitBoxDrops(*scope);
    emitBoxParameterDrops();
}

void IrGenerator::emitBoxParameterDrops() {
    for (const auto& [name, owner] : boxParameters_)
        if (!movedBoxes_.contains(name))
            ir_.instructions.push_back(IrDrop{owner.first, owner.second});
}

void IrGenerator::emitTailExpression(
    const Expression& expressionNode,
    const std::unordered_map<std::string, ValueId>& parameters,
    const Declaration& function) {
    if (const auto* block = std::get_if<BlockExpr>(&expressionNode.value)) {
        std::vector<std::string> localNames;
        boxScopes_.emplace_back();
        for (const StatementPtr& statement : block->statements) {
            std::visit([&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Declaration>) {
                    localNames.push_back(item.name);
                    if (valueTypeNeedsDrop(item.type))
                        boxScopes_.back().push_back(item.name);
                    if (item.kind == BindingKind::Def) {
                        symbols_.emplace(item.name, Symbol{0, item.kind, &item, false, item.name});
                    } else {
                        const ValueId value = expression(*item.initializer, parameters);
                        if (isMoveOnlyValueType(item.type))
                            if (const auto* source =
                                    std::get_if<NameExpr>(&item.initializer->value))
                                movedBoxes_.insert(source->name);
                        if (valueTypeNeedsDrop(item.type) && isCopyValueType(item.type) &&
                            std::holds_alternative<NameExpr>(item.initializer->value))
                            ir_.instructions.push_back(IrRetain{value, item.type});
                        const SlotId slot = ir_.slots.size();
                        symbols_.emplace(item.name, Symbol{slot, item.kind, &item, false, item.name});
                        ir_.slots.push_back(IrSlot{item.name, item.type, false});
                        ir_.instructions.push_back(IrStore{slot, value, item.type});
                    }
                } else if constexpr (std::is_same_v<T, Assignment>) {
                    const auto found = symbols_.find(item.name);
                    const ValueId value = expression(*item.value, parameters);
                    ir_.instructions.push_back(IrStore{found->second.slot, value,
                                                       found->second.declaration->type});
                } else if constexpr (std::is_same_v<T, IndexAssignment>) {
                    emitIndexStore(item, parameters);
                } else if constexpr (std::is_same_v<T, FieldAssignment>) {
                    emitFieldStore(item, parameters);
                } else if constexpr (std::is_same_v<T, DereferenceAssignment>) {
                    const ValueId reference = expression(*item.reference, parameters);
                    const ValueId value = expression(*item.value, parameters);
                    ir_.instructions.push_back(IrDereferenceStore{
                        reference, value, item.value->inferredType});
                } else if constexpr (std::is_same_v<T, WhileStatement>) {
                    emitLoop(item, parameters);
                } else if constexpr (std::is_same_v<T, ExpressionStatement>) {
                    expression(*item.expression, parameters);
                } else if constexpr (std::is_same_v<T, ReturnStatement>) {
                    const ValueId value = expression(*item.value, parameters);
                    if (isMoveOnlyValueType(item.value->inferredType))
                        if (const auto* moved = std::get_if<NameExpr>(&item.value->value))
                            movedBoxes_.insert(moved->name);
                    emitAllBoxDrops();
                    ir_.instructions.push_back(IrReturn{value, item.value->inferredType});
                } else if constexpr (std::is_same_v<T, BreakStatement>) {
                    emitBoxDrops(boxScopes_.back());
                    ir_.instructions.push_back(IrJump{loopLabels_.back().second});
                } else {
                    emitBoxDrops(boxScopes_.back());
                    ir_.instructions.push_back(IrJump{loopLabels_.back().first});
                }
            }, statement->value);
        }
        const bool ownsBox = std::any_of(localNames.begin(), localNames.end(), [&](const auto& name) {
            const auto symbol = symbols_.find(name);
            return symbol != symbols_.end() &&
                valueTypeNeedsDrop(symbol->second.declaration->type) &&
                !movedBoxes_.contains(name);
        });
        if (block->result && ownsBox) {
            const ValueId result = expression(*block->result, parameters);
            emitBoxDrops(localNames);
            emitBoxParameterDrops();
            ir_.instructions.push_back(IrReturn{result, function.type});
        } else if (block->result) {
            emitTailExpression(*block->result, parameters, function);
        }
        for (auto name = localNames.rbegin(); name != localNames.rend(); ++name)
            symbols_.erase(*name);
        boxScopes_.pop_back();
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
        ir_.instructions.push_back(IrTailCall{symbols_.at(function.name).linkName, std::move(arguments),
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
    const ValueType resolvedFunctionType = resolveType(function.type);
    if (isMoveOnlyValueType(resolvedFunctionType))
        if (const auto* moved = std::get_if<NameExpr>(&expressionNode.value))
            movedBoxes_.insert(moved->name);
    emitBoxParameterDrops();
    ir_.instructions.push_back(IrReturn{result, resolvedFunctionType});
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
    boxScopes_.emplace_back();
    for (const StatementPtr& statement : loop.body) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, Declaration>) {
                localNames.push_back(item.name);
                if (valueTypeNeedsDrop(item.type))
                    boxScopes_.back().push_back(item.name);
                if (item.kind == BindingKind::Def) {
                    symbols_.emplace(item.name, Symbol{0, item.kind, &item, false, item.name});
                } else {
                    const ValueId value = expression(*item.initializer, parameters);
                    if (isMoveOnlyValueType(item.type))
                        if (const auto* source =
                                std::get_if<NameExpr>(&item.initializer->value))
                            movedBoxes_.insert(source->name);
                    if (valueTypeNeedsDrop(item.type) && isCopyValueType(item.type) &&
                        std::holds_alternative<NameExpr>(item.initializer->value))
                        ir_.instructions.push_back(IrRetain{value, item.type});
                    const SlotId slot = ir_.slots.size();
                    symbols_.emplace(item.name, Symbol{slot, item.kind, &item, false, item.name});
                    ir_.slots.push_back(IrSlot{item.name, item.type, false});
                    ir_.instructions.push_back(IrStore{slot, value, item.type});
                }
            } else if constexpr (std::is_same_v<T, Assignment>) {
                const auto found = symbols_.find(item.name);
                const ValueId value = expression(*item.value, parameters);
                ir_.instructions.push_back(IrStore{found->second.slot, value,
                                                   found->second.declaration->type});
            } else if constexpr (std::is_same_v<T, IndexAssignment>) {
                emitIndexStore(item, parameters);
            } else if constexpr (std::is_same_v<T, FieldAssignment>) {
                emitFieldStore(item, parameters);
            } else if constexpr (std::is_same_v<T, DereferenceAssignment>) {
                const ValueId reference = expression(*item.reference, parameters);
                const ValueId value = expression(*item.value, parameters);
                ir_.instructions.push_back(IrDereferenceStore{
                    reference, value, item.value->inferredType});
            } else if constexpr (std::is_same_v<T, WhileStatement>) {
                emitLoop(item, parameters);
            } else if constexpr (std::is_same_v<T, ExpressionStatement>) {
                expression(*item.expression, parameters);
            } else if constexpr (std::is_same_v<T, ReturnStatement>) {
                const ValueId value = expression(*item.value, parameters);
                if (isMoveOnlyValueType(item.value->inferredType))
                    if (const auto* moved = std::get_if<NameExpr>(&item.value->value))
                        movedBoxes_.insert(moved->name);
                emitAllBoxDrops();
                ir_.instructions.push_back(IrReturn{value, item.value->inferredType});
            } else if constexpr (std::is_same_v<T, BreakStatement>) {
                emitBoxDrops(boxScopes_.back());
                ir_.instructions.push_back(IrJump{endLabel});
            } else {
                emitBoxDrops(boxScopes_.back());
                ir_.instructions.push_back(IrJump{conditionLabel});
            }
        }, statement->value);
    }
    emitBoxDrops(boxScopes_.back());
    ir_.instructions.push_back(IrJump{conditionLabel});
    ir_.instructions.push_back(IrLabel{endLabel});
    loopLabels_.pop_back();
    for (auto name = localNames.rbegin(); name != localNames.rend(); ++name)
        symbols_.erase(*name);
    boxScopes_.pop_back();
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
        } else if constexpr (std::is_same_v<T, CharacterExpr>) {
            const ValueId output = nextValue(ValueType::Char);
            ir_.instructions.push_back(IrConst{output, static_cast<std::int32_t>(node.value),
                                               ValueType::Char});
            return output;
        } else if constexpr (std::is_same_v<T, StringExpr>) {
            const ValueId output = nextValue(ValueType::String);
            ir_.instructions.push_back(IrStringConst{output, node.utf8});
            return output;
        } else if constexpr (std::is_same_v<T, ArrayExpr>) {
            std::vector<ValueId> elements;
            for (const ExprPtr& element : node.elements)
                elements.push_back(expression(*element, parameters));
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrArrayConstruct{output, std::move(elements),
                                                        expressionNode.inferredType});
            return output;
        } else if constexpr (std::is_same_v<T, StructExpr>) {
            std::vector<ValueId> fields;
            for (const ExprPtr& field : node.fields) fields.push_back(expression(*field, parameters));
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrStructConstruct{output, std::move(fields), expressionNode.inferredType});
            return output;
        } else if constexpr (std::is_same_v<T, EnumExpr>) {
            std::vector<ValueId> fields;
            const EnumVariant& variant = node.type->variants[node.variant];
            for (std::size_t i = 0; i < node.fields.size(); ++i) {
                const ExprPtr& field = node.fields[i];
                fields.push_back(expression(*field, parameters));
                if (isMoveOnlyValueType(variant.fields[i].type))
                    if (const auto* source = std::get_if<NameExpr>(&field->value))
                        movedBoxes_.insert(source->name);
            }
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrEnumConstruct{
                output, node.variant, std::move(fields), resolveType(expressionNode.inferredType)});
            return output;
        } else if constexpr (std::is_same_v<T, FieldExpr>) {
            const ValueId object = expression(*node.object, parameters);
            if (node.object->inferredType == ValueType::String) {
                const ValueId output = nextValue(expressionNode.inferredType);
                if (node.field == "lengthBytes")
                    ir_.instructions.push_back(IrStringLength{output, object});
                else
                    ir_.instructions.push_back(IrStringEmpty{output, object});
                return output;
            }
            if (node.object->inferredType.kind == ValueType::Kind::Slice) {
                const ValueId output = nextValue(ValueType::Int);
                ir_.instructions.push_back(IrSliceLength{output, object});
                return output;
            }
            const auto& fields = node.object->inferredType.structure->fields;
            const auto found = std::find_if(fields.begin(), fields.end(), [&](const StructField& field) { return field.name == node.field; });
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrFieldLoad{output, object, node.object->inferredType,
                static_cast<std::size_t>(found - fields.begin())});
            return output;
        } else if constexpr (std::is_same_v<T, IndexExpr>) {
            const ValueId array = expression(*node.array, parameters);
            const ValueId index = expression(*node.index, parameters);
            const ValueId output = nextValue(expressionNode.inferredType);
            const bool arrayIsReference =
                node.array->inferredType.kind == ValueType::Kind::Reference;
            const bool arrayIsSlice =
                node.array->inferredType.kind == ValueType::Kind::Slice;
            const ValueType resolvedOperandType = resolveType(node.array->inferredType);
            const ValueType arrayType = arrayIsReference
                ? *resolvedOperandType.element : resolvedOperandType;
            ir_.instructions.push_back(IrIndexLoad{output, array, index, arrayType,
                                                   arrayIsReference, arrayIsSlice});
            return output;
        } else if constexpr (std::is_same_v<T, AddressExpr>) {
            if (const auto* dereference = std::get_if<DereferenceExpr>(&node.operand->value)) {
                const ValueId pointer = expression(*dereference->operand, parameters);
                const ValueId output = nextValue(expressionNode.inferredType);
                ir_.instructions.push_back(IrCopy{output, pointer,
                                                  expressionNode.inferredType});
                return output;
            }
            const auto& name = std::get<NameExpr>(node.operand->value);
            const auto found = symbols_.find(name.name);
            if (found == symbols_.end())
                throw CompileError(expressionNode.location, "cible d'emprunt inconnue");
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrAddressOf{output, found->second.slot});
            return output;
        } else if constexpr (std::is_same_v<T, DereferenceExpr>) {
            const ValueId reference = expression(*node.operand, parameters);
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrDereference{output, reference,
                                                     expressionNode.inferredType});
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
            std::string linkName = found->second.linkName;
            auto callSubstitutions = typeSubstitutions_;
            if (!function.typeParameters.empty()) {
                std::vector<ValueType> instanceTypes;
                for (const ValueType& type : node.typeArguments)
                    instanceTypes.push_back(resolveType(type));
                registerGenericInstance(function, instanceTypes);
                linkName = genericLinkName(function, instanceTypes);
                callSubstitutions.clear();
                for (std::size_t i = 0; i < function.typeParameters.size(); ++i)
                    callSubstitutions.emplace(function.typeParameters[i], instanceTypes[i]);
            }
            const auto outerSubstitutions = typeSubstitutions_;
            typeSubstitutions_ = callSubstitutions;
            for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                arguments.push_back(expression(*node.arguments[i], parameters));
                const ValueType argumentType = resolveType(function.parameters[i].type);
                argumentTypes.push_back(argumentType);
                if (isMoveOnlyValueType(argumentType))
                    if (const auto* moved = std::get_if<NameExpr>(&node.arguments[i]->value))
                        movedBoxes_.insert(moved->name);
            }
            const ValueType returnType = resolveType(function.type);
            const ValueId output = nextValue(returnType);
            typeSubstitutions_ = outerSubstitutions;
            ir_.instructions.push_back(IrCall{output, linkName, std::move(arguments),
                                              std::move(argumentTypes), returnType});
            return output;
        } else if constexpr (std::is_same_v<T, ConversionExpr>) {
            const ValueId input = expression(*node.operand, parameters);
            const ValueId output = nextValue(node.target);
            if (node.target.kind == ValueType::Kind::Slice) {
                ir_.instructions.push_back(IrSliceConstruct{
                    output, input, node.operand->inferredType.element->length, node.target});
            } else if (node.target.kind == ValueType::Kind::Box) {
                ir_.instructions.push_back(IrBoxConstruct{output, input, *node.target.element});
            } else {
                ir_.instructions.push_back(IrConvert{output, input,
                                                      node.operand->inferredType, node.target});
            }
            return output;
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            const ValueId operand = expression(*node.operand, parameters);
            const ValueId output = nextValue(expressionNode.inferredType);
            ir_.instructions.push_back(IrUnary{output, node.op, operand,
                                               resolveType(expressionNode.inferredType)});
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
            if (expressionNode.inferredType == ValueType::String && node.op == "+")
                ir_.instructions.push_back(IrStringConcat{output, left, right});
            else
                ir_.instructions.push_back(IrBinary{output, node.op, left, right,
                                                    resolveType(expressionNode.inferredType),
                                                    resolveType(node.left->inferredType)});
            return output;
        } else if constexpr (std::is_same_v<T, BlockExpr>) {
            std::vector<std::string> localNames;
            boxScopes_.emplace_back();
            for (const StatementPtr& statement : node.statements) {
                std::visit([&](const auto& item) {
                    using S = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<S, Declaration>) {
                        localNames.push_back(item.name);
                        if (valueTypeNeedsDrop(item.type))
                            boxScopes_.back().push_back(item.name);
                        if (item.kind == BindingKind::Def) {
                            symbols_.emplace(item.name, Symbol{0, item.kind, &item, false, item.name});
                        } else {
                            const ValueId value = expression(*item.initializer, parameters);
                            if (isMoveOnlyValueType(item.type))
                                if (const auto* source =
                                        std::get_if<NameExpr>(&item.initializer->value))
                                    movedBoxes_.insert(source->name);
                            if (valueTypeNeedsDrop(item.type) && isCopyValueType(item.type) &&
                                std::holds_alternative<NameExpr>(item.initializer->value))
                                ir_.instructions.push_back(IrRetain{value, item.type});
                            const SlotId slot = ir_.slots.size();
                            symbols_.emplace(item.name, Symbol{slot, item.kind, &item, false, item.name});
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
                    } else if constexpr (std::is_same_v<S, IndexAssignment>) {
                        emitIndexStore(item, parameters);
                    } else if constexpr (std::is_same_v<S, FieldAssignment>) {
                        emitFieldStore(item, parameters);
                    } else if constexpr (std::is_same_v<S, DereferenceAssignment>) {
                        const ValueId reference = expression(*item.reference, parameters);
                        const ValueId value = expression(*item.value, parameters);
                        ir_.instructions.push_back(IrDereferenceStore{
                            reference, value, item.value->inferredType});
                    } else if constexpr (std::is_same_v<S, WhileStatement>) {
                        emitLoop(item, parameters);
                    } else if constexpr (std::is_same_v<S, ExpressionStatement>) {
                        expression(*item.expression, parameters);
                    } else if constexpr (std::is_same_v<S, ReturnStatement>) {
                        const ValueId value = expression(*item.value, parameters);
                        if (isMoveOnlyValueType(item.value->inferredType))
                            if (const auto* moved = std::get_if<NameExpr>(&item.value->value))
                                movedBoxes_.insert(moved->name);
                        emitAllBoxDrops();
                        ir_.instructions.push_back(IrReturn{value, item.value->inferredType});
                    } else if constexpr (std::is_same_v<S, BreakStatement>) {
                        emitBoxDrops(boxScopes_.back());
                        ir_.instructions.push_back(IrJump{loopLabels_.back().second});
                    } else {
                        emitBoxDrops(boxScopes_.back());
                        ir_.instructions.push_back(IrJump{loopLabels_.back().first});
                    }
                }, statement->value);
            }
            const ValueId result = node.result ? expression(*node.result, parameters)
                                               : nextValue(expressionNode.inferredType);
            emitBoxDrops(localNames);
            for (auto name = localNames.rbegin(); name != localNames.rend(); ++name) {
                symbols_.erase(*name);
            }
            boxScopes_.pop_back();
            return result;
        } else if constexpr (std::is_same_v<T, IfExpr>) {
            const ValueId condition = expression(*node.condition, parameters);
            const ValueId output = nextValue(expressionNode.inferredType);
            const ValueType resultType = resolveType(expressionNode.inferredType);
            const std::size_t elseLabel = nextLabel_++;
            const std::size_t endLabel = nextLabel_++;
            ir_.instructions.push_back(IrBranch{condition, false, elseLabel});
            const ValueId thenValue = expression(*node.thenBranch, parameters);
            ir_.instructions.push_back(IrCopy{output, thenValue, resultType});
            ir_.instructions.push_back(IrJump{endLabel});
            ir_.instructions.push_back(IrLabel{elseLabel});
            const ValueId elseValue = expression(*node.elseBranch, parameters);
            ir_.instructions.push_back(IrCopy{output, elseValue, resultType});
            ir_.instructions.push_back(IrLabel{endLabel});
            return output;
        } else {
            const ValueId input = expression(*node.operand, parameters);
            const ValueType resolvedEnum = resolveType(ValueType(node.type));
            const bool copiesInput = isCopyValueType(resolvedEnum);
            if (!copiesInput)
                if (const auto* moved = std::get_if<NameExpr>(&node.operand->value))
                    movedBoxes_.insert(moved->name);
            const ValueId tag = nextValue(ValueType::Int);
            ir_.instructions.push_back(IrEnumTag{tag, input});
            const ValueId output = nextValue(expressionNode.inferredType);
            const std::size_t endLabel = nextLabel_++;
            for (std::size_t branchIndex = 0; branchIndex < node.branches.size();
                 ++branchIndex) {
                const MatchBranch& branch = node.branches[branchIndex];
                const bool last = branchIndex + 1U == node.branches.size();
                std::size_t nextBranchLabel = 0;
                if (!last) {
                    nextBranchLabel = nextLabel_++;
                    const ValueId expectedTag = nextValue(ValueType::Int);
                    ir_.instructions.push_back(IrConst{
                        expectedTag, static_cast<std::int32_t>(branch.variant), ValueType::Int});
                    const ValueId matches = nextValue(ValueType::Bool);
                    ir_.instructions.push_back(IrBinary{
                        matches, "==", tag, expectedTag, ValueType::Bool, ValueType::Int});
                    ir_.instructions.push_back(IrBranch{matches, false, nextBranchLabel});
                }

                auto branchParameters = parameters;
                const EnumVariant& variant =
                    resolvedEnum.enumeration->variants[branch.variant];
                std::vector<std::string> branchOwners;
                for (std::size_t i = 0; i < branch.bindings.size(); ++i) {
                    if (!branch.bindings[i] && !valueTypeNeedsDrop(variant.fields[i].type))
                        continue;
                    const ValueType fieldType = resolveType(variant.fields[i].type);
                    const ValueId field = nextValue(fieldType);
                    ir_.instructions.push_back(IrEnumFieldLoad{
                        field, input, resolvedEnum, branch.variant, i});
                    if (copiesInput && valueTypeNeedsDrop(fieldType))
                        ir_.instructions.push_back(IrRetain{field, fieldType});
                    if (branch.bindings[i])
                        branchParameters.insert_or_assign(*branch.bindings[i], field);
                    if (valueTypeNeedsDrop(fieldType)) {
                        const std::string ownerName = branch.bindings[i]
                            ? *branch.bindings[i]
                            : "__match_ignored_" + std::to_string(endLabel) + "_" +
                              std::to_string(branchIndex) + "_" + std::to_string(i);
                        branchOwners.push_back(ownerName);
                        boxParameters_.insert_or_assign(
                            ownerName, std::pair{field, fieldType});
                    }
                }
                const ValueId branchValue = expression(*branch.result, branchParameters);
                if (const auto* returned = std::get_if<NameExpr>(&branch.result->value))
                    if (std::find(branchOwners.begin(), branchOwners.end(), returned->name) !=
                        branchOwners.end())
                        movedBoxes_.insert(returned->name);
                ir_.instructions.push_back(IrCopy{
                    output, branchValue, resolveType(expressionNode.inferredType)});
                for (const std::string& ownerName : branchOwners) {
                    const auto owner = boxParameters_.find(ownerName);
                    if (owner != boxParameters_.end() && !movedBoxes_.contains(ownerName))
                        ir_.instructions.push_back(IrDrop{owner->second.first, owner->second.second});
                    boxParameters_.erase(ownerName);
                    movedBoxes_.erase(ownerName);
                }
                ir_.instructions.push_back(IrJump{endLabel});
                if (!last) ir_.instructions.push_back(IrLabel{nextBranchLabel});
            }
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
        if (type == ValueType::Bool) return "i1";
        if (type == ValueType::Char) return "u32";
        return "string";
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
            else if constexpr (std::is_same_v<T, IrStringConst>)
                out << "  $" << item.output << " = const.string " << item.utf8.size()
                    << " bytes\n";
            else if constexpr (std::is_same_v<T, IrStringConcat>)
                out << "  $" << item.output << " = concat $" << item.left
                    << ", $" << item.right << '\n';
            else if constexpr (std::is_same_v<T, IrStringLength>)
                out << "  $" << item.output << " = string_length $" << item.string << '\n';
            else if constexpr (std::is_same_v<T, IrStringEmpty>)
                out << "  $" << item.output << " = string_empty $" << item.string << '\n';
            else if constexpr (std::is_same_v<T, IrArrayConstruct>) {
                out << "  $" << item.output << " = array " << typeName(item.type) << " [";
                for (std::size_t i = 0; i < item.elements.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << '$' << item.elements[i];
                }
                out << "]\n";
            }
            else if constexpr (std::is_same_v<T, IrStructConstruct>) {
                out << "  $" << item.output << " = struct " << typeName(item.type) << " [";
                for (std::size_t i = 0; i < item.fields.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << '$' << item.fields[i];
                }
                out << "]\n";
            }
            else if constexpr (std::is_same_v<T, IrEnumConstruct>) {
                out << "  $" << item.output << " = enum " << typeName(item.type) << '.'
                    << item.type.enumeration->variants[item.variant].name << " [";
                for (std::size_t i = 0; i < item.fields.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << '$' << item.fields[i];
                }
                out << "]\n";
            }
            else if constexpr (std::is_same_v<T, IrEnumTag>)
                out << "  $" << item.output << " = enum_tag $" << item.input << '\n';
            else if constexpr (std::is_same_v<T, IrEnumFieldLoad>)
                out << "  $" << item.output << " = enum_field $" << item.input << '.'
                    << item.type.enumeration->variants[item.variant].name << '.'
                    << item.type.enumeration->variants[item.variant].fields[item.field].name
                    << '\n';
            else if constexpr (std::is_same_v<T, IrFieldLoad>)
                out << "  $" << item.output << " = field $" << item.object
                    << '.' << item.objectType.structure->fields[item.field].name << '\n';
            else if constexpr (std::is_same_v<T, IrFieldStore>)
                out << "  field_store %" << program.slots[item.slot].name << '.'
                    << item.objectType.structure->fields[item.field].name << ", $" << item.value << '\n';
            else if constexpr (std::is_same_v<T, IrSliceConstruct>)
                out << "  $" << item.output << " = slice $" << item.reference
                    << ", " << item.length << '\n';
            else if constexpr (std::is_same_v<T, IrSliceLength>)
                out << "  $" << item.output << " = length $" << item.slice << '\n';
            else if constexpr (std::is_same_v<T, IrBoxConstruct>)
                out << "  $" << item.output << " = box $" << item.value << '\n';
            else if constexpr (std::is_same_v<T, IrIndexLoad>)
                out << "  $" << item.output << " = index " << '$' << item.array
                    << "[$" << item.index << "]\n";
            else if constexpr (std::is_same_v<T, IrIndexStore>)
            {
                if (item.arrayIsReference)
                    out << "  index_store $" << item.array;
                else
                    out << "  index_store %" << program.slots[item.slot].name;
                for (ValueId index : item.indexes) out << "[$" << index << ']';
                out << ", $" << item.value << '\n';
            }
            else if constexpr (std::is_same_v<T, IrAddressOf>)
                out << "  $" << item.output << " = address %"
                    << program.slots[item.slot].name << '\n';
            else if constexpr (std::is_same_v<T, IrDereference>)
                out << "  $" << item.output << " = dereference $" << item.reference << '\n';
            else if constexpr (std::is_same_v<T, IrDereferenceStore>)
                out << "  dereference_store $" << item.reference << ", $" << item.value << '\n';
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
            else if constexpr (std::is_same_v<T, IrDrop>)
                out << "  drop $" << item.value << " : " << typeName(item.type) << '\n';
            else if constexpr (std::is_same_v<T, IrRetain>)
                out << "  retain $" << item.value << " : " << typeName(item.type) << '\n';
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

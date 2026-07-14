#include "ir_verifier.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {
struct VisitedDefinitions {
    std::unordered_set<const StructType*> structures;
    std::unordered_set<const EnumType*> enumerations;
};

[[noreturn]] void fail(const std::string& code, const std::string& message) {
    throw IrVerificationError(code, message);
}

void verifyType(const ValueType& type, const std::string& context,
                VisitedDefinitions& visited) {
    using Kind = ValueType::Kind;
    if (type.kind == Kind::TypeParameter)
        fail("IRV003", context + " : paramètre générique non substitué '" +
             type.typeParameter + "'");

    if (type.kind == Kind::Array || type.kind == Kind::Reference ||
        type.kind == Kind::Slice || type.kind == Kind::Box || type.kind == Kind::Vec) {
        if (type.element == nullptr)
            fail("IRV002", context + " : type composé sans type élément");
        verifyType(*type.element, context + " -> élément", visited);
        return;
    }

    if (type.kind == Kind::Struct) {
        if (type.structure == nullptr)
            fail("IRV002", context + " : structure sans définition");
        if (!visited.structures.insert(type.structure.get()).second) return;
        for (std::size_t index = 0; index < type.structure->fields.size(); ++index)
            verifyType(type.structure->fields[index].type,
                       context + " -> champ " + std::to_string(index), visited);
        return;
    }

    if (type.kind == Kind::Enum) {
        if (type.enumeration == nullptr)
            fail("IRV002", context + " : enum sans définition");
        if (!visited.enumerations.insert(type.enumeration.get()).second) return;
        for (std::size_t variant = 0; variant < type.enumeration->variants.size(); ++variant)
            for (std::size_t field = 0;
                 field < type.enumeration->variants[variant].fields.size(); ++field)
                verifyType(type.enumeration->variants[variant].fields[field].type,
                           context + " -> variante " + std::to_string(variant) +
                               ", champ " + std::to_string(field),
                           visited);
    }
}

std::optional<ValueId> outputOf(const IrInstruction& instruction) {
    return std::visit([](const auto& item) -> std::optional<ValueId> {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, IrConst> ||
                      std::is_same_v<T, IrDoubleConst> ||
                      std::is_same_v<T, IrStringConst> ||
                      std::is_same_v<T, IrStringConcat> ||
                      std::is_same_v<T, IrStringLength> ||
                      std::is_same_v<T, IrStringEmpty> ||
                      std::is_same_v<T, IrArrayConstruct> ||
                      std::is_same_v<T, IrVecConstruct> ||
                      std::is_same_v<T, IrVecProperty> ||
                      std::is_same_v<T, IrVecReserve> ||
                      std::is_same_v<T, IrVecPush> ||
                      std::is_same_v<T, IrVecClear> ||
                      std::is_same_v<T, IrVecView> ||
                      std::is_same_v<T, IrVecGet> ||
                      std::is_same_v<T, IrVecPop> ||
                      std::is_same_v<T, IrVecSet> ||
                      std::is_same_v<T, IrStructConstruct> ||
                      std::is_same_v<T, IrEnumConstruct> ||
                      std::is_same_v<T, IrEnumTag> ||
                      std::is_same_v<T, IrEnumFieldLoad> ||
                      std::is_same_v<T, IrFieldLoad> ||
                      std::is_same_v<T, IrSliceConstruct> ||
                      std::is_same_v<T, IrSliceLength> ||
                      std::is_same_v<T, IrBoxConstruct> ||
                      std::is_same_v<T, IrIndexLoad> ||
                      std::is_same_v<T, IrAddressOf> ||
                      std::is_same_v<T, IrDereference> ||
                      std::is_same_v<T, IrLoad> ||
                      std::is_same_v<T, IrConvert> ||
                      std::is_same_v<T, IrUnary> ||
                      std::is_same_v<T, IrBinary> ||
                      std::is_same_v<T, IrCopy> ||
                      std::is_same_v<T, IrCall> ||
                      std::is_same_v<T, IrParameter>)
            return item.output;
        return std::nullopt;
    }, instruction);
}

std::vector<ValueId> readsOf(const IrInstruction& instruction) {
    return std::visit([](const auto& item) -> std::vector<ValueId> {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, IrStringConcat> ||
                      std::is_same_v<T, IrBinary>)
            return {item.left, item.right};
        else if constexpr (std::is_same_v<T, IrStringLength> ||
                           std::is_same_v<T, IrStringEmpty>)
            return {item.string};
        else if constexpr (std::is_same_v<T, IrArrayConstruct>)
            return item.elements;
        else if constexpr (std::is_same_v<T, IrVecProperty>)
            return {item.vector};
        else if constexpr (std::is_same_v<T, IrVecReserve>)
            return {item.additional};
        else if constexpr (std::is_same_v<T, IrVecPush>)
            return {item.value};
        else if constexpr (std::is_same_v<T, IrVecGet>)
            return {item.index};
        else if constexpr (std::is_same_v<T, IrVecSet>)
            return {item.index, item.value};
        else if constexpr (std::is_same_v<T, IrStructConstruct> ||
                           std::is_same_v<T, IrEnumConstruct>)
            return item.fields;
        else if constexpr (std::is_same_v<T, IrEnumTag>)
            return {item.input};
        else if constexpr (std::is_same_v<T, IrEnumFieldLoad>)
            return {item.input};
        else if constexpr (std::is_same_v<T, IrFieldLoad>)
            return {item.object};
        else if constexpr (std::is_same_v<T, IrFieldStore>)
            return {item.value};
        else if constexpr (std::is_same_v<T, IrSliceConstruct>)
            return {item.reference};
        else if constexpr (std::is_same_v<T, IrSliceLength>)
            return {item.slice};
        else if constexpr (std::is_same_v<T, IrBoxConstruct>)
            return {item.value};
        else if constexpr (std::is_same_v<T, IrIndexLoad>)
            return {item.array, item.index};
        else if constexpr (std::is_same_v<T, IrIndexStore>) {
            std::vector<ValueId> reads = item.indexes;
            if (item.arrayIsReference || item.arrayIsSlice)
                reads.insert(reads.begin(), item.array);
            reads.push_back(item.value);
            return reads;
        } else if constexpr (std::is_same_v<T, IrDereference>)
            return {item.reference};
        else if constexpr (std::is_same_v<T, IrDereferenceStore>)
            return {item.reference, item.value};
        else if constexpr (std::is_same_v<T, IrConvert>)
            return {item.input};
        else if constexpr (std::is_same_v<T, IrUnary>)
            return {item.operand};
        else if constexpr (std::is_same_v<T, IrStore>)
            return {item.value};
        else if constexpr (std::is_same_v<T, IrCopy>)
            return {item.input};
        else if constexpr (std::is_same_v<T, IrCall> ||
                           std::is_same_v<T, IrTailCall>)
            return item.arguments;
        else if constexpr (std::is_same_v<T, IrReturn> ||
                           std::is_same_v<T, IrDrop> ||
                           std::is_same_v<T, IrRetain>)
            return {item.value};
        else if constexpr (std::is_same_v<T, IrExit>)
            return {item.value};
        else if constexpr (std::is_same_v<T, IrBranch>)
            return {item.condition};
        return {};
    }, instruction);
}

std::vector<SlotId> slotsOf(const IrInstruction& instruction) {
    return std::visit([](const auto& item) -> std::vector<SlotId> {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, IrVecReserve> ||
                      std::is_same_v<T, IrVecPush> ||
                      std::is_same_v<T, IrVecClear> ||
                      std::is_same_v<T, IrVecView> ||
                      std::is_same_v<T, IrVecGet> ||
                      std::is_same_v<T, IrVecPop> ||
                      std::is_same_v<T, IrVecSet> ||
                      std::is_same_v<T, IrFieldStore> ||
                      std::is_same_v<T, IrAddressOf> ||
                      std::is_same_v<T, IrLoad> ||
                      std::is_same_v<T, IrStore>)
            return {item.slot};
        else if constexpr (std::is_same_v<T, IrIndexStore>) {
            if (!item.arrayIsReference && !item.arrayIsSlice) return {item.slot};
        }
        return {};
    }, instruction);
}

std::optional<std::size_t> targetOf(const IrInstruction& instruction) {
    if (const auto* branch = std::get_if<IrBranch>(&instruction)) return branch->label;
    if (const auto* jump = std::get_if<IrJump>(&instruction)) return jump->label;
    return std::nullopt;
}

std::string instructionContext(std::size_t instruction, const std::string& region) {
    return "instruction " + std::to_string(instruction) + ", région '" + region + "'";
}

std::size_t parameterStackBytes(const ValueType& type) {
    if (type.kind == ValueType::Kind::Struct || type.kind == ValueType::Kind::Enum)
        return (valueTypeSize(type) + 7U) / 8U * 8U;
    if (type == ValueType::String || type == ValueType::StringView ||
        type.kind == ValueType::Kind::Slice)
        return 16U;
    return 8U;
}

bool isTerminal(const IrInstruction& instruction) {
    return std::holds_alternative<IrJump>(instruction) ||
        std::holds_alternative<IrReturn>(instruction) ||
        std::holds_alternative<IrTailCall>(instruction) ||
        std::holds_alternative<IrExit>(instruction);
}
}

IrVerificationError::IrVerificationError(std::string code, const std::string& message)
    : std::runtime_error("[" + code + "] " + message), code_(std::move(code)) {}

void IrVerifier::verify(const IrProgram& program, IrVerificationMode mode) {
    static_assert(std::variant_size_v<IrInstruction> == 47,
                  "mettre à jour l'inventaire du vérificateur d'IR");
    static_cast<void>(mode);
    if (program.valueCount != program.valueTypes.size())
        fail("IRV001", "valueCount=" + std::to_string(program.valueCount) +
             " mais valueTypes.size()=" + std::to_string(program.valueTypes.size()));

    VisitedDefinitions visited;
    for (std::size_t index = 0; index < program.valueTypes.size(); ++index)
        verifyType(program.valueTypes[index],
                   "type de la valeur $" + std::to_string(index), visited);
    for (std::size_t index = 0; index < program.slots.size(); ++index)
        verifyType(program.slots[index].type,
                   "type du slot %" + std::to_string(index), visited);

    std::vector<std::size_t> instructionRegions(program.instructions.size(), 0);
    std::vector<std::string> regionNames{"<init>"};
    std::vector<std::optional<std::size_t>> regionEntries(1);
    std::unordered_set<std::string> functionNames;
    std::unordered_map<std::size_t, std::size_t> labelRegions;
    std::unordered_map<std::size_t, std::size_t> labelInstructions;
    std::vector<std::optional<std::size_t>> valueRegions(program.valueCount);
    std::vector<std::vector<std::size_t>> valueProducers(program.valueCount);
    std::vector<std::optional<std::size_t>> slotRegions(program.slots.size());
    std::size_t region = 0;
    bool parameterPreamble = false;
    std::size_t expectedParameter = 0;
    std::size_t expectedStackOffset = 16U;

    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        const IrInstruction& instruction = program.instructions[index];
        if (const auto* function = std::get_if<IrFunctionStart>(&instruction)) {
            if (function->name.empty() || !functionNames.insert(function->name).second ||
                (function->linkOnce && function->genericIdentity.empty()))
                fail("IRV010", "instruction " + std::to_string(index) +
                     " : frontière de fonction invalide '" + function->name + "'");
            region = regionNames.size();
            regionNames.push_back(function->name);
            regionEntries.push_back(index);
            parameterPreamble = true;
            expectedParameter = 0;
            expectedStackOffset = 16U;
        }
        if (region == 0 && !regionEntries[0].has_value()) regionEntries[0] = index;
        instructionRegions[index] = region;
        const std::string context = instructionContext(index, regionNames[region]);

        if (const auto* label = std::get_if<IrLabel>(&instruction)) {
            if (!labelRegions.emplace(label->label, region).second)
                fail("IRV050", context + " : label " +
                     std::to_string(label->label) + " dupliqué");
            labelInstructions.emplace(label->label, index);
        }

        if (const std::optional<ValueId> output = outputOf(instruction)) {
            if (*output >= program.valueCount)
                fail("IRV020", context + " : sortie $" + std::to_string(*output) +
                     " hors limites");
            if (valueRegions[*output].has_value() && *valueRegions[*output] != region)
                fail("IRV012", context + " : valeur $" + std::to_string(*output) +
                     " produite dans plusieurs régions");
            valueRegions[*output] = region;
            valueProducers[*output].push_back(index);
        }

        if (const auto* parameter = std::get_if<IrParameter>(&instruction)) {
            if (region == 0 || !parameterPreamble || parameter->index != expectedParameter ||
                parameter->stackOffset != expectedStackOffset)
                fail("IRV011", context + " : paramètre " +
                     std::to_string(parameter->index) + " ou offset " +
                     std::to_string(parameter->stackOffset) + " invalide");
            VisitedDefinitions parameterVisited;
            verifyType(parameter->type, context + " : type du paramètre", parameterVisited);
            ++expectedParameter;
            expectedStackOffset += parameterStackBytes(parameter->type);
        } else if (!std::holds_alternative<IrFunctionStart>(instruction)) {
            parameterPreamble = false;
        }

        for (const SlotId slot : slotsOf(instruction)) {
            if (slot >= program.slots.size())
                fail("IRV030", context + " : slot %" + std::to_string(slot) +
                     " hors limites");
            if (program.slots[slot].global) continue;
            if (slotRegions[slot].has_value() && *slotRegions[slot] != region)
                fail("IRV013", context + " : slot local %" + std::to_string(slot) +
                     " utilisé dans plusieurs régions");
            slotRegions[slot] = region;
        }
    }

    for (ValueId value = 0; value < valueProducers.size(); ++value) {
        if (valueProducers[value].size() < 2) continue;
        const bool copiesOnly = std::all_of(
            valueProducers[value].begin(), valueProducers[value].end(),
            [&](const std::size_t producer) {
                return std::holds_alternative<IrCopy>(program.instructions[producer]);
            });
        if (!copiesOnly)
            fail("IRV022", "valeur $" + std::to_string(value) +
                 " produite aux instructions " +
                 std::to_string(valueProducers[value].front()) + " et " +
                 std::to_string(valueProducers[value][1]) +
                 " hors fusion IrCopy");
    }

    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        const IrInstruction& instruction = program.instructions[index];
        region = instructionRegions[index];
        const std::string context = instructionContext(index, regionNames[region]);
        for (const ValueId value : readsOf(instruction)) {
            if (value >= program.valueCount)
                fail("IRV020", context + " : lecture de $" + std::to_string(value) +
                     " hors limites");
            if (valueRegions[value].has_value() && *valueRegions[value] != region)
                fail("IRV012", context + " : lecture de $" + std::to_string(value) +
                     " depuis une autre région");
        }
        if (const std::optional<std::size_t> target = targetOf(instruction)) {
            const auto found = labelRegions.find(*target);
            if (found == labelRegions.end() || found->second != region)
                fail("IRV050", context + " : cible label " +
                     std::to_string(*target) + " absente de la région");
        }
    }

    std::vector<std::vector<std::size_t>> successors(program.instructions.size());
    std::vector<std::vector<std::size_t>> predecessors(program.instructions.size());
    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        const IrInstruction& instruction = program.instructions[index];
        const std::size_t instructionRegion = instructionRegions[index];
        if (const auto* jump = std::get_if<IrJump>(&instruction))
            successors[index].push_back(labelInstructions.at(jump->label));
        else {
            if (const auto* branch = std::get_if<IrBranch>(&instruction))
                successors[index].push_back(labelInstructions.at(branch->label));
            if (!isTerminal(instruction) && index + 1 < program.instructions.size() &&
                instructionRegions[index + 1] == instructionRegion)
                successors[index].push_back(index + 1);
        }
        for (const std::size_t successor : successors[index])
            predecessors[successor].push_back(index);
    }

    std::vector<unsigned char> reachable(program.instructions.size(), 0);
    std::deque<std::size_t> pending;
    for (const std::optional<std::size_t> entry : regionEntries)
        if (entry.has_value()) pending.push_back(*entry);
    while (!pending.empty()) {
        const std::size_t index = pending.front();
        pending.pop_front();
        if (reachable[index] != 0) continue;
        reachable[index] = 1;
        for (const std::size_t successor : successors[index]) pending.push_back(successor);
    }

    using Definitions = std::vector<unsigned char>;
    std::vector<Definitions> definitionsIn(
        program.instructions.size(), Definitions(program.valueCount, 1));
    std::vector<Definitions> definitionsOut = definitionsIn;
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t index = 0; index < program.instructions.size(); ++index) {
            if (reachable[index] == 0) continue;
            Definitions nextIn(program.valueCount, 0);
            const bool isEntry = regionEntries[instructionRegions[index]].has_value() &&
                *regionEntries[instructionRegions[index]] == index;
            if (!isEntry && !predecessors[index].empty()) {
                nextIn = definitionsOut[predecessors[index].front()];
                for (std::size_t predecessorIndex = 1;
                     predecessorIndex < predecessors[index].size(); ++predecessorIndex) {
                    const Definitions& incoming =
                        definitionsOut[predecessors[index][predecessorIndex]];
                    for (ValueId value = 0; value < program.valueCount; ++value)
                        nextIn[value] = static_cast<unsigned char>(
                            nextIn[value] != 0 && incoming[value] != 0);
                }
            }
            Definitions nextOut = nextIn;
            if (const std::optional<ValueId> output = outputOf(program.instructions[index]))
                nextOut[*output] = 1;
            if (nextIn != definitionsIn[index] || nextOut != definitionsOut[index]) {
                definitionsIn[index] = std::move(nextIn);
                definitionsOut[index] = std::move(nextOut);
                changed = true;
            }
        }
    }

    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        if (reachable[index] == 0) continue;
        const std::string context = instructionContext(
            index, regionNames[instructionRegions[index]]);
        for (const ValueId value : readsOf(program.instructions[index]))
            if (definitionsIn[index][value] == 0)
                fail("IRV023", context + " : valeur $" + std::to_string(value) +
                     " utilisée avant définition sur tous les chemins");
    }
}

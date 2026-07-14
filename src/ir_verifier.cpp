#include "ir_verifier.hpp"
#include "type_rules.hpp"

#include <algorithm>
#include <cstdint>
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

bool requiresTypeVerification(const ValueType& type) {
    using Kind = ValueType::Kind;
    return type.kind == Kind::Array || type.kind == Kind::Reference ||
        type.kind == Kind::Slice || type.kind == Kind::Box || type.kind == Kind::Vec ||
        type.kind == Kind::TypeParameter || type.kind == Kind::Struct ||
        type.kind == Kind::Enum;
}

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
        if constexpr (std::is_same_v<T, IrUnit> ||
                      std::is_same_v<T, IrConst> ||
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

void verifyEmbeddedType(const ValueType& type, const std::string& context) {
    VisitedDefinitions visited;
    verifyType(type, context, visited);
}

void expectValueType(const IrProgram& program, ValueId value,
                     const ValueType& expected, const std::string& context) {
    if (program.valueTypes[value] != expected)
        fail("IRV040", context + " : type de $" + std::to_string(value) +
             " incompatible, " + typeName(program.valueTypes[value]) +
             " au lieu de " + typeName(expected));
}

void expectOutputType(const IrProgram& program, ValueId output,
                      const ValueType& expected, const std::string& context) {
    if (program.valueTypes[output] != expected)
        fail("IRV021", context + " : sortie $" + std::to_string(output) +
             " de type " + typeName(program.valueTypes[output]) +
             " au lieu de " + typeName(expected));
}

void expectSlotType(const IrProgram& program, SlotId slot,
                    const ValueType& expected, const std::string& context) {
    if (program.slots[slot].type != expected)
        fail("IRV031", context + " : slot %" + std::to_string(slot) +
             " de type " + typeName(program.slots[slot].type) +
             " au lieu de " + typeName(expected));
}

void expectVecTargetType(const IrProgram& program, SlotId slot,
                         std::optional<std::size_t> field,
                         const ValueType& expected, const std::string& context) {
    if (!field) {
        expectSlotType(program, slot, expected, context);
        return;
    }
    const ValueType& owner = program.slots[slot].type;
    if (owner.kind != ValueType::Kind::Struct || *field >= owner.structure->fields.size())
        fail("IRV031", context + " : cible de champ Vec invalide");
    if (owner.structure->fields[*field].type != expected)
        fail("IRV031", context + " : champ cible de type " +
            typeName(owner.structure->fields[*field].type) + " au lieu de " +
            typeName(expected));
}

bool isBuiltinOptionOf(const ValueType& option, const ValueType& element) {
    if (option.kind != ValueType::Kind::Enum || option.enumeration == nullptr) return false;
    const EnumType* definition = option.enumeration->genericDefinition != nullptr
        ? option.enumeration->genericDefinition.get() : option.enumeration.get();
    if (definition != builtinOptionType().get() || option.enumeration->typeArguments.size() != 1 ||
        option.enumeration->typeArguments.front() != element)
        return false;
    const auto& variants = option.enumeration->variants;
    const auto some = std::find_if(variants.begin(), variants.end(), [](const EnumVariant& variant) {
        return variant.name == "Some";
    });
    const auto none = std::find_if(variants.begin(), variants.end(), [](const EnumVariant& variant) {
        return variant.name == "None";
    });
    return some != variants.end() && some->fields.size() == 1 &&
        some->fields.front().type == element && none != variants.end() && none->fields.empty();
}

bool canRetain(const ValueType& type) {
    if (type == ValueType::String) return true;
    if (type.kind == ValueType::Kind::Array) return canRetain(*type.element);
    if (type.kind == ValueType::Kind::Struct)
        return std::any_of(type.structure->fields.begin(), type.structure->fields.end(),
            [](const StructField& field) { return canRetain(field.type); });
    if (type.kind == ValueType::Kind::Enum)
        return std::any_of(type.enumeration->variants.begin(), type.enumeration->variants.end(),
            [](const EnumVariant& variant) {
                return std::any_of(variant.fields.begin(), variant.fields.end(),
                    [](const StructField& field) { return canRetain(field.type); });
            });
    return false;
}

void verifyInstructionTypes(const IrProgram& program, const IrInstruction& instruction,
                            const std::string& context) {
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        const auto concrete = [&](const ValueType& type) {
            if (requiresTypeVerification(type))
                verifyEmbeddedType(type, context + " : type d'instruction");
        };
        if constexpr (std::is_same_v<T, IrUnit>) {
            expectOutputType(program, item.output, ValueType::Unit, context);
        } else if constexpr (std::is_same_v<T, IrConst>) {
            concrete(item.type);
            if (item.type != ValueType::Int && item.type != ValueType::Byte &&
                item.type != ValueType::Bool && item.type != ValueType::Char)
                fail("IRV040", context + " : type de constante invalide");
            if ((item.type == ValueType::Byte && (item.value < 0 || item.value > 255)) ||
                (item.type == ValueType::Bool && item.value != 0 && item.value != 1) ||
                (item.type == ValueType::Char &&
                 (item.value < 0 || item.value > 0x10ffff ||
                  (item.value >= 0xd800 && item.value <= 0xdfff))))
                fail("IRV040", context + " : valeur de constante invalide");
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrDoubleConst>) {
            expectOutputType(program, item.output, ValueType::Double, context);
        } else if constexpr (std::is_same_v<T, IrStringConst>) {
            expectOutputType(program, item.output, ValueType::String, context);
        } else if constexpr (std::is_same_v<T, IrStringConcat>) {
            expectValueType(program, item.left, ValueType::String, context);
            expectValueType(program, item.right, ValueType::String, context);
            expectOutputType(program, item.output, ValueType::String, context);
        } else if constexpr (std::is_same_v<T, IrStringLength>) {
            const ValueType& string = program.valueTypes[item.string];
            if (string != ValueType::String && string != ValueType::StringView)
                fail("IRV040", context + " : longueur appliquée hors chaîne ou vue");
            expectOutputType(program, item.output, ValueType::Int, context);
        } else if constexpr (std::is_same_v<T, IrStringEmpty>) {
            const ValueType& string = program.valueTypes[item.string];
            if (string != ValueType::String && string != ValueType::StringView)
                fail("IRV040", context + " : isEmpty appliqué hors chaîne ou vue");
            expectOutputType(program, item.output, ValueType::Bool, context);
        } else if constexpr (std::is_same_v<T, IrArrayConstruct>) {
            concrete(item.type);
            if (item.type.kind != ValueType::Kind::Array ||
                item.elements.size() != item.type.length)
                fail("IRV044", context + " : construction de tableau invalide");
            for (ValueId element : item.elements)
                expectValueType(program, element, *item.type.element, context);
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrVecConstruct>) {
            concrete(item.type);
            if (item.type.kind != ValueType::Kind::Vec)
                fail("IRV044", context + " : construction Vec invalide");
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrVecProperty>) {
            const ValueType& vector = program.valueTypes[item.vector];
            if (vector.kind != ValueType::Kind::Vec)
                fail("IRV040", context + " : propriété appliquée hors Vec");
            if (item.property == "length" || item.property == "capacity")
                expectOutputType(program, item.output, ValueType::Int, context);
            else if (item.property == "isEmpty")
                expectOutputType(program, item.output, ValueType::Bool, context);
            else fail("IRV041", context + " : propriété Vec inconnue '" + item.property + "'");
        } else if constexpr (std::is_same_v<T, IrVecReserve> ||
                             std::is_same_v<T, IrVecPush> ||
                             std::is_same_v<T, IrVecClear>) {
            concrete(item.type);
            if (item.type.kind != ValueType::Kind::Vec)
                fail("IRV044", context + " : opération Vec sans type Vec");
            expectVecTargetType(program, item.slot, item.field, item.type, context);
            if constexpr (std::is_same_v<T, IrVecReserve>)
                expectValueType(program, item.additional, ValueType::Int, context);
            if constexpr (std::is_same_v<T, IrVecPush>)
                expectValueType(program, item.value, *item.type.element, context);
            expectOutputType(program, item.output, ValueType::Int, context);
        } else if constexpr (std::is_same_v<T, IrVecView>) {
            concrete(item.type);
            const ValueType& vector = program.slots[item.slot].type;
            if (vector.kind != ValueType::Kind::Vec || item.type.kind != ValueType::Kind::Slice ||
                *vector.element != *item.type.element)
                fail("IRV040", context + " : vue Vec incompatible");
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrVecGet> ||
                             std::is_same_v<T, IrVecPop>) {
            concrete(item.optionType);
            concrete(item.elementType);
            const ValueType& vector = program.slots[item.slot].type;
            if (vector.kind != ValueType::Kind::Vec || *vector.element != item.elementType ||
                !isBuiltinOptionOf(item.optionType, item.elementType))
                fail("IRV040", context + " : contrat get/pop de Vec invalide");
            if constexpr (std::is_same_v<T, IrVecGet>)
                expectValueType(program, item.index, ValueType::Int, context);
            expectOutputType(program, item.output, item.optionType, context);
        } else if constexpr (std::is_same_v<T, IrVecSet>) {
            concrete(item.elementType);
            const ValueType vector(ValueType::Kind::Vec,
                std::make_shared<ValueType>(item.elementType));
            expectVecTargetType(program, item.slot, item.field, vector, context);
            expectValueType(program, item.index, ValueType::Int, context);
            expectValueType(program, item.value, item.elementType, context);
            expectOutputType(program, item.output, ValueType::Int, context);
        } else if constexpr (std::is_same_v<T, IrStructConstruct>) {
            concrete(item.type);
            if (item.type.kind != ValueType::Kind::Struct ||
                item.fields.size() != item.type.structure->fields.size())
                fail("IRV044", context + " : construction de structure invalide");
            for (std::size_t i = 0; i < item.fields.size(); ++i)
                expectValueType(program, item.fields[i], item.type.structure->fields[i].type, context);
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrEnumConstruct>) {
            concrete(item.type);
            if (item.type.kind != ValueType::Kind::Enum ||
                item.variant >= item.type.enumeration->variants.size() ||
                item.fields.size() != item.type.enumeration->variants[item.variant].fields.size())
                fail("IRV044", context + " : construction d'enum invalide");
            const auto& fields = item.type.enumeration->variants[item.variant].fields;
            for (std::size_t i = 0; i < item.fields.size(); ++i)
                expectValueType(program, item.fields[i], fields[i].type, context);
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrEnumTag>) {
            if (program.valueTypes[item.input].kind != ValueType::Kind::Enum)
                fail("IRV040", context + " : tag appliqué hors enum");
            expectOutputType(program, item.output, ValueType::Int, context);
        } else if constexpr (std::is_same_v<T, IrEnumFieldLoad>) {
            concrete(item.type);
            if (item.type.kind != ValueType::Kind::Enum ||
                item.variant >= item.type.enumeration->variants.size() ||
                item.field >= item.type.enumeration->variants[item.variant].fields.size())
                fail("IRV044", context + " : champ d'enum invalide");
            expectValueType(program, item.input, item.type, context);
            expectOutputType(program, item.output,
                item.type.enumeration->variants[item.variant].fields[item.field].type, context);
        } else if constexpr (std::is_same_v<T, IrFieldLoad>) {
            concrete(item.objectType);
            if (item.objectType.kind != ValueType::Kind::Struct ||
                item.field >= item.objectType.structure->fields.size())
                fail("IRV044", context + " : champ de structure invalide");
            expectValueType(program, item.object, item.objectType, context);
            expectOutputType(program, item.output,
                item.objectType.structure->fields[item.field].type, context);
        } else if constexpr (std::is_same_v<T, IrFieldStore>) {
            concrete(item.objectType);
            if (item.objectType.kind != ValueType::Kind::Struct ||
                item.field >= item.objectType.structure->fields.size())
                fail("IRV044", context + " : stockage de champ invalide");
            expectSlotType(program, item.slot, item.objectType, context);
            expectValueType(program, item.value,
                item.objectType.structure->fields[item.field].type, context);
        } else if constexpr (std::is_same_v<T, IrSliceConstruct>) {
            concrete(item.type);
            const ValueType& reference = program.valueTypes[item.reference];
            if (item.type.kind != ValueType::Kind::Slice ||
                reference.kind != ValueType::Kind::Reference ||
                reference.element->kind != ValueType::Kind::Array ||
                *reference.element->element != *item.type.element ||
                reference.mutableReference != item.type.mutableReference ||
                reference.element->length != item.length)
                fail("IRV040", context + " : construction de slice invalide");
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrSliceLength>) {
            if (program.valueTypes[item.slice].kind != ValueType::Kind::Slice)
                fail("IRV040", context + " : longueur appliquée hors slice");
            expectOutputType(program, item.output, ValueType::Int, context);
        } else if constexpr (std::is_same_v<T, IrBoxConstruct>) {
            concrete(item.elementType);
            expectValueType(program, item.value, item.elementType, context);
            expectOutputType(program, item.output,
                ValueType(ValueType::Kind::Box, std::make_shared<const ValueType>(item.elementType)),
                context);
        } else if constexpr (std::is_same_v<T, IrIndexLoad>) {
            concrete(item.arrayType);
            if (item.arrayIsReference && item.arrayIsSlice)
                fail("IRV045", context + " : drapeaux d'indexation incompatibles");
            const ValueType& source = program.valueTypes[item.array];
            const ValueType* container = &item.arrayType;
            if (item.arrayIsReference) {
                if (source.kind != ValueType::Kind::Reference ||
                    *source.element != item.arrayType)
                    fail("IRV040", context + " : tableau référencé incompatible");
            } else if (item.arrayIsSlice) {
                if (item.arrayType.kind != ValueType::Kind::Slice || source != item.arrayType)
                    fail("IRV040", context + " : slice indexée incompatible");
            } else if (source != item.arrayType) {
                fail("IRV040", context + " : tableau indexé incompatible");
            }
            if (container->kind != ValueType::Kind::Array &&
                container->kind != ValueType::Kind::Slice)
                fail("IRV044", context + " : type non indexable");
            expectValueType(program, item.index, ValueType::Int, context);
            expectOutputType(program, item.output, *container->element, context);
        } else if constexpr (std::is_same_v<T, IrIndexStore>) {
            concrete(item.arrayType);
            if (item.arrayIsReference && item.arrayIsSlice)
                fail("IRV045", context + " : drapeaux d'indexation incompatibles");
            if (item.indexes.empty())
                fail("IRV044", context + " : stockage indexé sans indice");
            if (item.arrayIsReference) {
                const ValueType& source = program.valueTypes[item.array];
                if (source.kind != ValueType::Kind::Reference || !source.mutableReference ||
                    *source.element != item.arrayType)
                    fail("IRV040", context + " : référence indexée non mutable ou incompatible");
            } else if (item.arrayIsSlice) {
                const ValueType& source = program.valueTypes[item.array];
                if (item.arrayType.kind != ValueType::Kind::Slice ||
                    !item.arrayType.mutableReference || source != item.arrayType)
                    fail("IRV040", context + " : slice indexée non mutable ou incompatible");
            } else {
                expectSlotType(program, item.slot, item.arrayType, context);
            }
            const ValueType* current = &item.arrayType;
            for (ValueId index : item.indexes) {
                expectValueType(program, index, ValueType::Int, context);
                if (current->kind != ValueType::Kind::Array &&
                    current->kind != ValueType::Kind::Slice)
                    fail("IRV044", context + " : profondeur d'indexation invalide");
                current = current->element.get();
            }
            expectValueType(program, item.value, *current, context);
        } else if constexpr (std::is_same_v<T, IrAddressOf>) {
            const ValueType& output = program.valueTypes[item.output];
            if (output.kind != ValueType::Kind::Reference ||
                *output.element != program.slots[item.slot].type)
                fail("IRV040", context + " : adresse de slot incompatible");
        } else if constexpr (std::is_same_v<T, IrDereference>) {
            concrete(item.type);
            const ValueType& reference = program.valueTypes[item.reference];
            if ((reference.kind != ValueType::Kind::Reference &&
                 reference.kind != ValueType::Kind::Box) || *reference.element != item.type)
                fail("IRV040", context + " : déréférencement incompatible");
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrDereferenceStore>) {
            concrete(item.type);
            const ValueType& reference = program.valueTypes[item.reference];
            const bool mutableReference = reference.kind == ValueType::Kind::Reference &&
                reference.mutableReference && *reference.element == item.type;
            const bool ownedBox = reference.kind == ValueType::Kind::Box &&
                *reference.element == item.type;
            if (!mutableReference && !ownedBox)
                fail("IRV040", context + " : écriture par référence incompatible");
            expectValueType(program, item.value, item.type, context);
        } else if constexpr (std::is_same_v<T, IrLoad>) {
            concrete(item.type);
            expectSlotType(program, item.slot, item.type, context);
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrStore>) {
            concrete(item.type);
            expectSlotType(program, item.slot, item.type, context);
            expectValueType(program, item.value, item.type, context);
        } else if constexpr (std::is_same_v<T, IrCopy>) {
            concrete(item.type);
            const ValueType& input = program.valueTypes[item.input];
            const bool boxBorrow = item.type.kind == ValueType::Kind::Reference &&
                input.kind == ValueType::Kind::Box && *item.type.element == *input.element;
            if (input != item.type && !boxBorrow)
                fail("IRV040", context + " : copie entre types incompatibles");
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrConvert>) {
            concrete(item.source);
            concrete(item.target);
            if (item.target.kind == ValueType::Kind::Slice ||
                item.target.kind == ValueType::Kind::Box ||
                !TypeRules::canExplicitlyConvert(item.source, item.target))
                fail("IRV041", context + " : conversion non supportée");
            expectValueType(program, item.input, item.source, context);
            expectOutputType(program, item.output, item.target, context);
        } else if constexpr (std::is_same_v<T, IrUnary>) {
            concrete(item.type);
            const bool valid = (item.op == "!" && item.type == ValueType::Bool) ||
                ((item.op == "+" || item.op == "-") && TypeRules::isNumeric(item.type));
            if (!valid) fail("IRV041", context + " : opérateur unaire invalide '" + item.op + "'");
            expectValueType(program, item.operand, item.type, context);
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrBinary>) {
            concrete(item.type);
            concrete(item.operandType);
            expectValueType(program, item.left, item.operandType, context);
            expectValueType(program, item.right, item.operandType, context);
            if (TypeRules::isLogical(item.op)) {
                if (item.type != ValueType::Bool || item.operandType != ValueType::Bool)
                    fail("IRV040", context + " : types logiques invalides");
            } else if (TypeRules::isComparison(item.op)) {
                const bool allowed = TypeRules::isEquality(item.op)
                    ? isEquatableValueType(item.operandType)
                    : TypeRules::isNumeric(item.operandType) ||
                        item.operandType == ValueType::Char;
                if (item.type != ValueType::Bool || !allowed)
                    fail("IRV041", context + " : comparaison invalide");
            } else if (item.op == "+" || item.op == "-" || item.op == "*" ||
                       item.op == "/") {
                if (!TypeRules::isNumeric(item.operandType) || item.type != item.operandType)
                    fail("IRV041", context + " : opération arithmétique invalide");
            } else fail("IRV041", context + " : opérateur binaire inconnu '" + item.op + "'");
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrCall>) {
            concrete(item.returnType);
            if (item.function.empty() || item.arguments.size() != item.argumentTypes.size())
                fail("IRV042", context + " : appel ou arité de types invalide");
            for (std::size_t i = 0; i < item.arguments.size(); ++i) {
                concrete(item.argumentTypes[i]);
                expectValueType(program, item.arguments[i], item.argumentTypes[i], context);
            }
            expectOutputType(program, item.output, item.returnType, context);
        } else if constexpr (std::is_same_v<T, IrTailCall>) {
            if (item.function.empty() || item.arguments.size() != item.argumentTypes.size())
                fail("IRV042", context + " : tail call ou arité de types invalide");
            for (std::size_t i = 0; i < item.arguments.size(); ++i) {
                concrete(item.argumentTypes[i]);
                expectValueType(program, item.arguments[i], item.argumentTypes[i], context);
            }
        } else if constexpr (std::is_same_v<T, IrParameter>) {
            concrete(item.type);
            expectOutputType(program, item.output, item.type, context);
        } else if constexpr (std::is_same_v<T, IrReturn> ||
                             std::is_same_v<T, IrDrop> ||
                             std::is_same_v<T, IrRetain>) {
            concrete(item.type);
            expectValueType(program, item.value, item.type, context);
            if constexpr (std::is_same_v<T, IrDrop>)
                if (!valueTypeNeedsDrop(item.type))
                    fail("IRV040", context + " : drop sur type sans ressource");
            if constexpr (std::is_same_v<T, IrRetain>)
                if (!canRetain(item.type))
                    fail("IRV040", context + " : retain sur type non retenable");
        } else if constexpr (std::is_same_v<T, IrExit>) {
            expectValueType(program, item.value, ValueType::Int, context);
        } else if constexpr (std::is_same_v<T, IrBranch>) {
            if (program.valueTypes[item.condition] != ValueType::Bool)
                fail("IRV051", context + " : condition de branche non Bool");
        }
    }, instruction);
}
}

IrVerificationError::IrVerificationError(std::string code, const std::string& message)
    : std::runtime_error("[" + code + "] " + message), code_(std::move(code)) {}

VerifiedIrProgram IrVerifier::verify(const IrProgram& program, IrVerificationMode mode) {
    static_assert(std::variant_size_v<IrInstruction> == 48,
                  "mettre à jour l'inventaire du vérificateur d'IR");
    if (program.valueCount != program.valueTypes.size())
        fail("IRV001", "valueCount=" + std::to_string(program.valueCount) +
             " mais valueTypes.size()=" + std::to_string(program.valueTypes.size()));

    VisitedDefinitions visited;
    for (std::size_t index = 0; index < program.valueTypes.size(); ++index)
        if (requiresTypeVerification(program.valueTypes[index]))
            verifyType(program.valueTypes[index],
                       "type de la valeur $" + std::to_string(index), visited);
    for (std::size_t index = 0; index < program.slots.size(); ++index)
        if (requiresTypeVerification(program.slots[index].type))
            verifyType(program.slots[index].type,
                       "type du slot %" + std::to_string(index), visited);

    for (std::size_t index = 0; index < program.slots.size(); ++index)
        if (program.slots[index].external && !program.slots[index].global)
            fail("IRV032", "slot %" + std::to_string(index) +
                 " : un slot externe doit être global");

    for (const IrInstruction& instruction : program.instructions) {
        const auto* exit = std::get_if<IrExit>(&instruction);
        if (exit == nullptr) continue;
        if (exit->value >= program.valueCount) continue;
        if (program.exitValue >= program.valueCount ||
            program.valueTypes[program.exitValue] != ValueType::Int ||
            exit->value != program.exitValue)
            fail("IRV004", "exitValue ne concorde pas avec l'opérande Int de IrExit");
    }

    std::vector<std::size_t> instructionRegions(program.instructions.size(), 0);
    std::vector<std::string> regionNames{"<init>"};
    std::vector<std::optional<std::size_t>> regionEntries(1);
    std::unordered_set<std::string> functionNames;
    std::unordered_map<std::size_t, std::size_t> labelRegions;
    std::unordered_map<std::size_t, std::size_t> labelInstructions;
    std::vector<std::optional<std::size_t>> valueRegions(program.valueCount);
    std::vector<std::vector<std::size_t>> valueProducers(program.valueCount);
    std::vector<std::optional<std::size_t>> slotRegions(program.slots.size());
    std::vector<std::optional<ValueId>> instructionOutputs;
    instructionOutputs.reserve(program.instructions.size());
    for (const IrInstruction& instruction : program.instructions)
        instructionOutputs.push_back(outputOf(instruction));
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
        const auto context = [&] { return instructionContext(index, regionNames[region]); };

        if (const auto* label = std::get_if<IrLabel>(&instruction)) {
            if (!labelRegions.emplace(label->label, region).second)
                fail("IRV050", context() + " : label " +
                     std::to_string(label->label) + " dupliqué");
            labelInstructions.emplace(label->label, index);
        }

        if (const std::optional<ValueId> output = instructionOutputs[index]) {
            if (*output >= program.valueCount)
                fail("IRV020", context() + " : sortie $" + std::to_string(*output) +
                     " hors limites");
            if (valueRegions[*output].has_value() && *valueRegions[*output] != region)
                fail("IRV012", context() + " : valeur $" + std::to_string(*output) +
                     " produite dans plusieurs régions");
            valueRegions[*output] = region;
            valueProducers[*output].push_back(index);
        }

        if (const auto* parameter = std::get_if<IrParameter>(&instruction)) {
            if (region == 0 || !parameterPreamble || parameter->index != expectedParameter ||
                parameter->stackOffset != expectedStackOffset)
                fail("IRV011", context() + " : paramètre " +
                     std::to_string(parameter->index) + " ou offset " +
                     std::to_string(parameter->stackOffset) + " invalide");
            if (requiresTypeVerification(parameter->type)) {
                VisitedDefinitions parameterVisited;
                verifyType(parameter->type, context() + " : type du paramètre",
                           parameterVisited);
            }
            ++expectedParameter;
            expectedStackOffset += parameterStackBytes(parameter->type);
        } else if (!std::holds_alternative<IrFunctionStart>(instruction)) {
            parameterPreamble = false;
        }

        for (const SlotId slot : slotsOf(instruction)) {
            if (slot >= program.slots.size())
                fail("IRV030", context() + " : slot %" + std::to_string(slot) +
                     " hors limites");
            if (program.slots[slot].global) continue;
            if (slotRegions[slot].has_value() && *slotRegions[slot] != region)
                fail("IRV013", context() + " : slot local %" + std::to_string(slot) +
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

    std::vector<ValueId> instructionReads;
    instructionReads.reserve(program.instructions.size());
    std::vector<std::size_t> instructionReadOffsets;
    instructionReadOffsets.reserve(program.instructions.size() + 1U);
    for (const IrInstruction& instruction : program.instructions) {
        instructionReadOffsets.push_back(instructionReads.size());
        const std::vector<ValueId> reads = readsOf(instruction);
        instructionReads.insert(instructionReads.end(), reads.begin(), reads.end());
    }
    instructionReadOffsets.push_back(instructionReads.size());
    static const std::string noContext;
    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        const IrInstruction& instruction = program.instructions[index];
        region = instructionRegions[index];
        const auto context = [&] { return instructionContext(index, regionNames[region]); };
        for (std::size_t read = instructionReadOffsets[index];
             read < instructionReadOffsets[index + 1U]; ++read) {
            const ValueId value = instructionReads[read];
            if (value >= program.valueCount)
                fail("IRV020", context() + " : lecture de $" + std::to_string(value) +
                     " hors limites");
            if (valueRegions[value].has_value() && *valueRegions[value] != region)
                fail("IRV012", context() + " : lecture de $" + std::to_string(value) +
                     " depuis une autre région");
        }
        if (const std::optional<std::size_t> target = targetOf(instruction)) {
            const auto found = labelRegions.find(*target);
            if (found == labelRegions.end() || found->second != region)
                fail("IRV050", context() + " : cible label " +
                     std::to_string(*target) + " absente de la région");
        }
        try {
            verifyInstructionTypes(program, instruction, noContext);
        } catch (const IrVerificationError&) {
            verifyInstructionTypes(program, instruction, context());
            throw;
        }
    }

    struct FunctionSignature {
        std::vector<ValueType> parameters;
        std::optional<ValueType> returnType;
    };
    std::unordered_map<std::string, FunctionSignature> signatures;
    std::string currentFunction;
    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        const IrInstruction& instruction = program.instructions[index];
        if (const auto* start = std::get_if<IrFunctionStart>(&instruction)) {
            currentFunction = start->name;
            signatures.emplace(currentFunction, FunctionSignature{});
        } else if (const auto* parameter = std::get_if<IrParameter>(&instruction)) {
            signatures.at(currentFunction).parameters.push_back(parameter->type);
        } else if (const auto* returned = std::get_if<IrReturn>(&instruction)) {
            if (currentFunction.empty())
                fail("IRV043", instructionContext(index, "<init>") +
                     " : retour hors fonction");
            std::optional<ValueType>& expected = signatures.at(currentFunction).returnType;
            if (expected.has_value() && *expected != returned->type)
                fail("IRV043", instructionContext(index, currentFunction) +
                     " : types de retour incohérents");
            expected = returned->type;
        }
    }

    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        const IrInstruction& instruction = program.instructions[index];
        const std::string& regionName = regionNames[instructionRegions[index]];
        const std::string context = instructionContext(index, regionName);
        if (const auto* call = std::get_if<IrCall>(&instruction)) {
            const auto found = signatures.find(call->function);
            if (found == signatures.end()) continue;
            if (call->argumentTypes != found->second.parameters ||
                (found->second.returnType.has_value() &&
                 call->returnType != *found->second.returnType))
                fail("IRV043", context + " : signature de l'appel interne '" +
                     call->function + "' incompatible");
        } else if (const auto* call = std::get_if<IrTailCall>(&instruction)) {
            const auto found = signatures.find(call->function);
            if (regionName == "<init>" || call->function != regionName ||
                found == signatures.end() || call->argumentTypes != found->second.parameters)
                fail("IRV043", context + " : signature du tail call incompatible");
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

    std::vector<std::size_t> traversalMarks(program.instructions.size(), 0);
    std::size_t traversal = 0;
    const auto reachesForward = [&](std::size_t from, std::size_t target,
                                    std::optional<std::size_t> blocked) {
        ++traversal;
        std::deque<std::size_t> work;
        for (const std::size_t successor : successors[from])
            if (successor > from) work.push_back(successor);
        while (!work.empty()) {
            const std::size_t index = work.front();
            work.pop_front();
            if (blocked.has_value() && index == *blocked) continue;
            if (index == target) return true;
            if (traversalMarks[index] == traversal) continue;
            traversalMarks[index] = traversal;
            for (const std::size_t successor : successors[index])
                if (successor > index) work.push_back(successor);
        }
        return false;
    };
    std::vector<std::vector<std::size_t>> valueReaders(program.valueCount);
    for (std::size_t index = 0; index < program.instructions.size(); ++index)
        for (std::size_t read = instructionReadOffsets[index];
             read < instructionReadOffsets[index + 1U]; ++read)
            valueReaders[instructionReads[read]].push_back(index);
    for (ValueId value = 0; value < valueProducers.size(); ++value) {
        const std::vector<std::size_t>& producers = valueProducers[value];
        if (producers.size() < 2) continue;
        for (const std::size_t producer : producers)
            if (reachable[producer] == 0)
                fail("IRV024", "pseudo-phi $" + std::to_string(value) +
                     " produit sur un chemin inatteignable");
        const auto verifyExclusiveOrder = [&](std::size_t first, std::size_t second) {
            if (!reachesForward(first, second, std::nullopt)) return;
            bool hasBypass = false;
            for (const std::size_t index : valueReaders[value]) {
                if (reachesForward(second, index, std::nullopt) &&
                    reachesForward(first, index, second)) {
                    hasBypass = true;
                    break;
                }
            }
            if (!hasBypass)
                    fail("IRV024", "pseudo-phi $" + std::to_string(value) +
                         " réécrit sans chemin exclusif");
        };
        for (std::size_t left = 0; left < producers.size(); ++left)
            for (std::size_t right = left + 1; right < producers.size(); ++right) {
                verifyExclusiveOrder(producers[left], producers[right]);
                verifyExclusiveOrder(producers[right], producers[left]);
            }
    }

    using Definitions = std::vector<std::uint64_t>;
    const std::size_t definitionWords = (program.valueCount + 63U) / 64U;
    const Definitions allDefinitions(definitionWords, ~std::uint64_t{0});
    std::vector<Definitions> definitionsIn(
        program.instructions.size(), allDefinitions);
    std::vector<Definitions> definitionsOut = definitionsIn;
    Definitions nextIn(definitionWords, 0);
    Definitions nextOut(definitionWords, 0);
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t index = 0; index < program.instructions.size(); ++index) {
            if (reachable[index] == 0) continue;
            std::fill(nextIn.begin(), nextIn.end(), 0);
            const bool isEntry = regionEntries[instructionRegions[index]].has_value() &&
                *regionEntries[instructionRegions[index]] == index;
            if (!isEntry && !predecessors[index].empty()) {
                nextIn = definitionsOut[predecessors[index].front()];
                for (std::size_t predecessorIndex = 1;
                     predecessorIndex < predecessors[index].size(); ++predecessorIndex) {
                    const Definitions& incoming =
                        definitionsOut[predecessors[index][predecessorIndex]];
                    for (std::size_t word = 0; word < definitionWords; ++word)
                        nextIn[word] &= incoming[word];
                }
            }
            nextOut = nextIn;
            if (const std::optional<ValueId> output = instructionOutputs[index])
                nextOut[*output / 64U] |= std::uint64_t{1} << (*output % 64U);
            if (nextIn != definitionsIn[index] || nextOut != definitionsOut[index]) {
                definitionsIn[index] = nextIn;
                definitionsOut[index] = nextOut;
                changed = true;
            }
        }
    }

    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        if (reachable[index] == 0) continue;
        for (std::size_t read = instructionReadOffsets[index];
             read < instructionReadOffsets[index + 1U]; ++read) {
            const ValueId value = instructionReads[read];
            if ((definitionsIn[index][value / 64U] &
                 (std::uint64_t{1} << (value % 64U))) == 0)
                fail("IRV023", instructionContext(
                         index, regionNames[instructionRegions[index]]) +
                     " : valeur $" + std::to_string(value) +
                     " utilisée avant définition sur tous les chemins");
        }
    }

    bool followsTerminal = false;
    std::size_t lexicalRegion = 0;
    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        const IrInstruction& instruction = program.instructions[index];
        const std::size_t instructionRegion = instructionRegions[index];
        if (instructionRegion != lexicalRegion ||
            std::holds_alternative<IrFunctionStart>(instruction)) {
            lexicalRegion = instructionRegion;
            followsTerminal = false;
        }
        if (std::holds_alternative<IrLabel>(instruction)) {
            followsTerminal = false;
            continue;
        }
        if (followsTerminal)
            fail("IRV052", instructionContext(index, regionNames[instructionRegion]) +
                 " : instruction après un terminal sans label");
        followsTerminal = isTerminal(instruction);
    }

    std::vector<unsigned char> validTerminal(program.instructions.size(), 0);
    for (std::size_t index = 0; index < program.instructions.size(); ++index) {
        const IrInstruction& instruction = program.instructions[index];
        const std::size_t instructionRegion = instructionRegions[index];
        const std::string context = instructionContext(index, regionNames[instructionRegion]);
        if (std::holds_alternative<IrReturn>(instruction) ||
            std::holds_alternative<IrTailCall>(instruction)) {
            if (instructionRegion == 0)
                fail("IRV054", context + " : retour ou tail call hors fonction");
            validTerminal[index] = 1;
        } else if (std::holds_alternative<IrExit>(instruction)) {
            if (instructionRegion != 0 || mode != IrVerificationMode::Executable)
                fail("IRV054", context + " : exit interdit dans cette région");
            validTerminal[index] = 1;
        } else if (instructionRegion == 0 && mode == IrVerificationMode::ModuleObject &&
                   reachable[index] != 0 && successors[index].empty()) {
            validTerminal[index] = 1;
        }
    }

    std::vector<unsigned char> canTerminate(program.instructions.size(), 0);
    std::deque<std::size_t> terminating;
    for (std::size_t index = 0; index < validTerminal.size(); ++index)
        if (validTerminal[index] != 0) terminating.push_back(index);
    while (!terminating.empty()) {
        const std::size_t index = terminating.front();
        terminating.pop_front();
        if (canTerminate[index] != 0) continue;
        canTerminate[index] = 1;
        for (const std::size_t predecessor : predecessors[index])
            if (instructionRegions[predecessor] == instructionRegions[index])
                terminating.push_back(predecessor);
    }
    for (std::size_t index = 0; index < program.instructions.size(); ++index)
        if (reachable[index] != 0 && canTerminate[index] == 0)
            fail("IRV053", instructionContext(index,
                 regionNames[instructionRegions[index]]) +
                 " : aucun terminal valide atteignable");
    return VerifiedIrProgram(program, mode);
}

#include "codegen.hpp"
#include "ir_verifier.hpp"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <unordered_set>

namespace {
std::string globalLabel(const IrSlot& slot) { return "zeta_global_" + slot.name; }
std::size_t align16(std::size_t value) { return (value + 15U) & ~std::size_t{15U}; }
bool isPairValue(ValueType type) {
    return type == ValueType::String || type == ValueType::StringView ||
        type.kind == ValueType::Kind::Slice;
}
bool isAggregateValue(ValueType type) {
    return type.kind == ValueType::Kind::Struct || type.kind == ValueType::Kind::Enum ||
        type.kind == ValueType::Kind::Vec;
}
std::size_t typeSize(ValueType type) {
    return valueTypeSize(type);
}
std::size_t valueSize(ValueType type) {
    if (type == ValueType::Never) return 0U;
    if (type == ValueType::Unit) return 0U;
    if (type.kind == ValueType::Kind::Array || isAggregateValue(type))
        return valueTypeSize(type);
    if (type.kind == ValueType::Kind::Reference || type.kind == ValueType::Kind::Box) return 8U;
    if (isPairValue(type)) return 16U;
    return type == ValueType::Double ? 8U : 4U;
}
std::size_t slotBytes(const IrProgram& program) {
    std::size_t size = 0;
    for (const IrSlot& slot : program.slots)
        if (!slot.global) size += typeSize(slot.type);
    return size;
}
std::size_t slotOffset(const IrProgram& program, SlotId target) {
    std::size_t offset = 0;
    for (SlotId slot = 0; slot <= target; ++slot)
        if (!program.slots[slot].global) offset += typeSize(program.slots[slot].type);
    return offset;
}
std::size_t valueOffset(const IrProgram& program, ValueId value) {
    std::size_t offset = slotBytes(program);
    for (ValueId current = 0; current <= value; ++current)
        offset += valueSize(program.valueTypes[current]);
    return offset;
}
std::size_t valueBytes(const IrProgram& program) {
    std::size_t size = 0;
    for (ValueType type : program.valueTypes) size += valueSize(type);
    return size;
}
std::string formatDouble(double value) {
    std::ostringstream out;
    out << std::scientific << std::setprecision(std::numeric_limits<double>::max_digits10)
        << value;
    return out.str();
}
std::string displacedAddress(const std::string& address, std::size_t displacement) {
    if (displacement == 0) return address;
    return address.substr(0, address.size() - 1) + "+" + std::to_string(displacement) + "]";
}
std::string vecSlotAddress(const IrProgram& program, SlotId slotId,
                           std::optional<std::size_t> field) {
    const IrSlot& slot = program.slots[slotId];
    std::string address = slot.global
        ? "[" + globalLabel(slot) + "]"
        : "[rbp-" + std::to_string(slotOffset(program, slotId)) + "]";
    if (field)
        address = displacedAddress(address, slot.type.structure->fields[*field].offset);
    return address;
}
std::string vecMutationAddress(const IrProgram& program,
                               const IrVecMutationTarget& target) {
    if (target.value)
        return "[rbp-" + std::to_string(valueOffset(program, *target.value)) + "]";
    if (target.reference) {
        std::string address = "[rbx]";
        if (target.field)
            address = displacedAddress(address,
                target.ownerType->structure->fields[*target.field].offset);
        return address;
    }
    return vecSlotAddress(program, *target.slot, target.field);
}
void emitVecMutationTargetSetup(std::ostringstream& out, const IrProgram& program,
                                const IrVecMutationTarget& target) {
    if (!target.reference) return;
    out << "    push rbx\n"
        << "    mov rbx, qword [rbp-" << valueOffset(program, *target.reference) << "]\n";
}
void emitVecMutationTargetCleanup(std::ostringstream& out,
                                  const IrVecMutationTarget& target) {
    if (target.reference) out << "    pop rbx\n";
}
void emitBlockCopy(std::ostringstream& out, const std::string& source,
                   const std::string& target, std::size_t bytes) {
    std::size_t offset = 0;
    while (bytes - offset >= 8U) {
        out << "    mov rax, qword " << displacedAddress(source, offset) << "\n"
            << "    mov qword " << displacedAddress(target, offset) << ", rax\n";
        offset += 8U;
    }
    if (bytes - offset >= 4U) {
        out << "    mov eax, dword " << displacedAddress(source, offset) << "\n"
            << "    mov dword " << displacedAddress(target, offset) << ", eax\n";
        offset += 4U;
    }
    while (offset < bytes) {
        out << "    mov al, byte " << displacedAddress(source, offset) << "\n"
            << "    mov byte " << displacedAddress(target, offset) << ", al\n";
        ++offset;
    }
}
void emitValueDrop(std::ostringstream& out, const std::string& address,
                   const ValueType& type, const std::string& label) {
    if (type == ValueType::String) {
        out << "    mov rdi, qword " << address << "\n"
            << "    cmp qword [rdi-16], -1\n"
            << "    je " << label << "_done\n"
            << "    dec qword [rdi-16]\n"
            << "    jnz " << label << "_done\n"
            << "    mov rsi, qword [rdi-8]\n"
            << "    add rsi, 16\n"
            << "    sub rdi, 16\n"
            << "    mov eax, 11\n"
            << "    syscall\n"
            << label << "_done:\n";
        return;
    }
    if (type.kind == ValueType::Kind::Box) {
        out << "    mov rdi, qword " << address << "\n"
            << "    push rdi\n";
        if (valueTypeNeedsDrop(*type.element))
            emitValueDrop(out, "[rdi]", *type.element, label + "_content");
        out << "    pop rdi\n"
            << "    mov rsi, " << valueTypeSize(*type.element) << "\n"
            << "    mov eax, 11\n"
            << "    syscall\n";
        return;
    }
    if (type.kind == ValueType::Kind::Vec) {
        const std::string done = label + "_done";
        const std::string loop = label + "_element_loop";
        const std::string release = label + "_release";
        out << "    push r12\n"
            << "    push r13\n"
            << "    push r14\n"
            << "    push r15\n"
            << "    mov r12, qword " << address << "\n"
            << "    test r12, r12\n"
            << "    jz " << done << "\n"
            << "    mov r15, qword " << displacedAddress(address, 16U) << "\n";
        if (valueTypeNeedsDrop(*type.element)) {
            out << "    mov r13, qword " << displacedAddress(address, 8U) << "\n"
                << loop << ":\n"
                << "    test r13, r13\n"
                << "    jz " << release << "\n"
                << "    dec r13\n"
                << "    mov r14, r13\n"
                << "    imul r14, " << valueTypeSize(*type.element) << "\n"
                << "    add r14, r12\n";
            emitValueDrop(out, "[r14]", *type.element, label + "_element");
            out << "    jmp " << loop << "\n";
        }
        out << release << ":\n"
            << "    mov rdi, r12\n"
            << "    mov rsi, r15\n"
            << "    imul rsi, " << valueTypeSize(*type.element) << "\n"
            << "    mov eax, 11\n"
            << "    syscall\n"
            << done << ":\n"
            << "    pop r15\n"
            << "    pop r14\n"
            << "    pop r13\n"
            << "    pop r12\n";
        return;
    }
    if (type.kind == ValueType::Kind::Array) {
        const std::size_t elementSize = valueTypeSize(*type.element);
        for (std::size_t i = 0; i < type.length; ++i)
            if (valueTypeNeedsDrop(*type.element))
                emitValueDrop(out, displacedAddress(address, i * elementSize), *type.element,
                              label + "_element_" + std::to_string(i));
        return;
    }
    if (type.kind == ValueType::Kind::Struct) {
        for (std::size_t i = 0; i < type.structure->fields.size(); ++i) {
            const StructField& field = type.structure->fields[i];
            if (valueTypeNeedsDrop(field.type))
                emitValueDrop(out, displacedAddress(address, field.offset), field.type,
                              label + "_field_" + std::to_string(i));
        }
        return;
    }
    if (type.kind == ValueType::Kind::Enum) {
        const std::string done = label + "_done";
        out << "    mov eax, dword " << address << "\n";
        for (std::size_t i = 0; i < type.enumeration->variants.size(); ++i)
            out << "    cmp eax, " << i << "\n"
                << "    je " << label << "_variant_" << i << "\n";
        out << "    jmp " << done << "\n";
        for (std::size_t i = 0; i < type.enumeration->variants.size(); ++i) {
            out << label << "_variant_" << i << ":\n";
            const EnumVariant& variant = type.enumeration->variants[i];
            for (std::size_t fieldIndex = 0; fieldIndex < variant.fields.size(); ++fieldIndex) {
                const StructField& field = variant.fields[fieldIndex];
                if (valueTypeNeedsDrop(field.type))
                    emitValueDrop(out, displacedAddress(address,
                        type.enumeration->payloadOffset + field.offset), field.type,
                        label + "_variant_" + std::to_string(i) + "_field_" +
                            std::to_string(fieldIndex));
            }
            out << "    jmp " << done << "\n";
        }
        out << done << ":\n";
    }
}

void emitValueRetain(std::ostringstream& out, const std::string& address,
                     const ValueType& type, const std::string& label) {
    if (type == ValueType::String) {
        out << "    mov rax, qword " << address << "\n"
            << "    cmp qword [rax-16], -1\n"
            << "    je " << label << "_done\n"
            << "    inc qword [rax-16]\n"
            << label << "_done:\n";
        return;
    }
    if (type.kind == ValueType::Kind::Array) {
        const std::size_t elementSize = valueTypeSize(*type.element);
        for (std::size_t i = 0; i < type.length; ++i)
            if (valueTypeNeedsDrop(*type.element))
                emitValueRetain(out, displacedAddress(address, i * elementSize), *type.element,
                                label + "_element_" + std::to_string(i));
        return;
    }
    if (type.kind == ValueType::Kind::Struct) {
        for (std::size_t i = 0; i < type.structure->fields.size(); ++i) {
            const StructField& field = type.structure->fields[i];
            if (valueTypeNeedsDrop(field.type))
                emitValueRetain(out, displacedAddress(address, field.offset), field.type,
                                label + "_field_" + std::to_string(i));
        }
        return;
    }
    if (type.kind == ValueType::Kind::Enum) {
        const std::string done = label + "_done";
        out << "    mov eax, dword " << address << "\n";
        for (std::size_t i = 0; i < type.enumeration->variants.size(); ++i)
            out << "    cmp eax, " << i << "\n"
                << "    je " << label << "_variant_" << i << "\n";
        out << "    jmp " << done << "\n";
        for (std::size_t i = 0; i < type.enumeration->variants.size(); ++i) {
            out << label << "_variant_" << i << ":\n";
            const EnumVariant& variant = type.enumeration->variants[i];
            for (std::size_t fieldIndex = 0; fieldIndex < variant.fields.size(); ++fieldIndex) {
                const StructField& field = variant.fields[fieldIndex];
                if (valueTypeNeedsDrop(field.type))
                    emitValueRetain(out, displacedAddress(address,
                        type.enumeration->payloadOffset + field.offset), field.type,
                        label + "_variant_" + std::to_string(i) + "_field_" +
                            std::to_string(fieldIndex));
            }
            out << "    jmp " << done << "\n";
        }
        out << done << ":\n";
    }
}

void emitEqualityChecks(std::ostringstream& out, const std::string& left,
                        const std::string& right, const ValueType& type,
                        const std::string& different, const std::string& label) {
    if (type == ValueType::String) {
        out << "    mov rcx, qword " << displacedAddress(left, 8U) << "\n"
            << "    cmp rcx, qword " << displacedAddress(right, 8U) << "\n"
            << "    jne " << different << "\n"
            << "    mov rsi, qword " << left << "\n"
            << "    mov rdi, qword " << right << "\n"
            << "    test rcx, rcx\n"
            << "    je " << label << "_equal\n"
            << label << "_loop:\n"
            << "    mov al, byte [rsi]\n"
            << "    cmp al, byte [rdi]\n"
            << "    jne " << different << "\n"
            << "    inc rsi\n    inc rdi\n    dec rcx\n"
            << "    jne " << label << "_loop\n"
            << label << "_equal:\n";
        return;
    }
    if (type == ValueType::Double) {
        out << "    movsd xmm0, qword " << left << "\n"
            << "    ucomisd xmm0, qword " << right << "\n"
            << "    jne " << different << "\n"
            << "    jp " << different << "\n";
        return;
    }
    if (type == ValueType::Byte || type == ValueType::Bool) {
        out << "    mov al, byte " << left << "\n"
            << "    cmp al, byte " << right << "\n"
            << "    jne " << different << "\n";
        return;
    }
    if (type == ValueType::Int || type == ValueType::Char) {
        out << "    mov eax, dword " << left << "\n"
            << "    cmp eax, dword " << right << "\n"
            << "    jne " << different << "\n";
        return;
    }
    if (type.kind == ValueType::Kind::Array) {
        const std::size_t elementSize = valueTypeSize(*type.element);
        for (std::size_t i = 0; i < type.length; ++i)
            emitEqualityChecks(out, displacedAddress(left, i * elementSize),
                displacedAddress(right, i * elementSize), *type.element, different,
                label + "_element_" + std::to_string(i));
        return;
    }
    if (type.kind == ValueType::Kind::Struct) {
        for (std::size_t i = 0; i < type.structure->fields.size(); ++i) {
            const StructField& field = type.structure->fields[i];
            emitEqualityChecks(out, displacedAddress(left, field.offset),
                displacedAddress(right, field.offset), field.type, different,
                label + "_field_" + std::to_string(i));
        }
        return;
    }
    if (type.kind == ValueType::Kind::Enum) {
        out << "    mov eax, dword " << left << "\n"
            << "    cmp eax, dword " << right << "\n"
            << "    jne " << different << "\n";
        const std::string done = label + "_done";
        for (std::size_t i = 0; i < type.enumeration->variants.size(); ++i)
            out << "    cmp eax, " << i << "\n"
                << "    je " << label << "_variant_" << i << "\n";
        out << "    jmp " << done << "\n";
        for (std::size_t i = 0; i < type.enumeration->variants.size(); ++i) {
            out << label << "_variant_" << i << ":\n";
            const EnumVariant& variant = type.enumeration->variants[i];
            for (std::size_t fieldIndex = 0; fieldIndex < variant.fields.size(); ++fieldIndex) {
                const StructField& field = variant.fields[fieldIndex];
                const std::size_t offset = type.enumeration->payloadOffset + field.offset;
                emitEqualityChecks(out, displacedAddress(left, offset),
                    displacedAddress(right, offset), field.type, different,
                    label + "_variant_" + std::to_string(i) + "_field_" +
                        std::to_string(fieldIndex));
            }
            out << "    jmp " << done << "\n";
        }
        out << done << ":\n";
    }
}
}

std::string LlvmIrCodeGenerator::generate(const IrProgram& program) {
    return generate(IrVerifier::verify(program, IrVerificationMode::Executable));
}

std::string LlvmIrCodeGenerator::generate(const VerifiedIrProgram& verified) {
    if (verified.mode() != IrVerificationMode::Executable)
        throw IrVerificationError("IRV004", "mode objet fourni au codegen LLVM exécutable");

    const IrProgram& program = verified.program();
    std::ostringstream out;
    std::unordered_map<ValueId, std::string> values;
    std::unordered_set<ValueId> heapStringValues;
    std::unordered_set<SlotId> heapStringSlots;
    using HeapStringPath = std::vector<std::size_t>;
    using HeapStringPaths = std::set<HeapStringPath>;
    std::unordered_map<ValueId, HeapStringPaths> heapStringValuePaths;
    std::unordered_map<SlotId, HeapStringPaths> heapStringSlotPaths;
    std::unordered_map<std::string, ValueType> functionReturnTypes;
    std::unordered_map<std::string, std::vector<ValueType>> functionParameterTypes;
    std::unordered_map<std::string, std::vector<std::string>> functionCalls;
    std::unordered_set<std::string> definedFunctions;
    std::vector<std::string> rootCalls;
    std::string scannedFunction;
    for (const IrInstruction& instruction : program.instructions) {
        if (const auto* function = std::get_if<IrFunctionStart>(&instruction)) {
            scannedFunction = function->name;
            definedFunctions.insert(function->name);
        } else if (!scannedFunction.empty()) {
            if (const auto* parameter = std::get_if<IrParameter>(&instruction)) {
                functionParameterTypes[scannedFunction].push_back(parameter->type);
            } else if (const auto* result = std::get_if<IrReturn>(&instruction)) {
                functionReturnTypes.insert_or_assign(scannedFunction, result->type);
            }
        }
        if (const auto* call = std::get_if<IrCall>(&instruction)) {
            if (scannedFunction.empty()) {
                rootCalls.push_back(call->function);
            } else {
                functionCalls[scannedFunction].push_back(call->function);
            }
        }
    }
    std::unordered_set<std::string> reachableFunctions;
    std::vector<std::string> pendingFunctions;
    for (const std::string& function : rootCalls) {
        if (definedFunctions.contains(function) && reachableFunctions.insert(function).second)
            pendingFunctions.push_back(function);
    }
    while (!pendingFunctions.empty()) {
        const std::string function = pendingFunctions.back();
        pendingFunctions.pop_back();
        for (const std::string& callee : functionCalls[function]) {
            if (definedFunctions.contains(callee) && reachableFunctions.insert(callee).second)
                pendingFunctions.push_back(callee);
        }
    }

    std::unordered_map<ValueId, std::size_t> copyDefinitionCounts;
    for (const IrInstruction& instruction : program.instructions) {
        if (const auto* copy = std::get_if<IrCopy>(&instruction)) {
            ++copyDefinitionCounts[copy->output];
        }
    }
    std::unordered_map<ValueId, ValueType> repeatedCopyTypes;
    for (const auto& [id, count] : copyDefinitionCounts) {
        if (count > 1) {
            const ValueType type = program.valueTypes.at(id);
            if (type != ValueType::Unit) repeatedCopyTypes.insert_or_assign(id, type);
        }
    }

    std::function<bool(const ValueType&)> isLlvmValueType;
    std::function<std::string(const ValueType&)> llvmType;
    isLlvmValueType = [&](const ValueType& type) -> bool {
        if (type == ValueType::Int || type == ValueType::Byte || type == ValueType::Bool ||
            type == ValueType::Char || type == ValueType::Double ||
            type == ValueType::String || type == ValueType::StringView)
            return true;
        if (type.kind == ValueType::Kind::Struct) {
            for (const StructField& field : type.structure->fields) {
                if (!isLlvmValueType(field.type)) return false;
            }
            return true;
        }
        return false;
    };
    llvmType = [&](const ValueType& type) -> std::string {
        if (type == ValueType::Int) return "i32";
        if (type == ValueType::Byte) return "i8";
        if (type == ValueType::Bool) return "i1";
        if (type == ValueType::Char) return "i32";
        if (type == ValueType::Double) return "double";
        if (type == ValueType::String || type == ValueType::StringView) return "{ ptr, i64 }";
        if (type.kind == ValueType::Kind::Struct && isLlvmValueType(type)) {
            std::ostringstream aggregate;
            aggregate << "{ ";
            for (std::size_t i = 0; i < type.structure->fields.size(); ++i) {
                if (i != 0) aggregate << ", ";
                aggregate << llvmType(type.structure->fields[i].type);
            }
            aggregate << " }";
            return aggregate.str();
        }
        throw std::runtime_error("backend LLVM: type non supporté " + typeName(type));
    };
    auto isLlvmUnsupportedAggregate = [&](const ValueType& type) -> bool {
        if (type.kind == ValueType::Kind::Struct && isLlvmValueType(type)) return false;
        return type.kind == ValueType::Kind::Array || type.kind == ValueType::Kind::Slice ||
            type.kind == ValueType::Kind::Box || type.kind == ValueType::Kind::Vec ||
            type.kind == ValueType::Kind::Struct || type.kind == ValueType::Kind::Enum;
    };
    auto llvmStringBytes = [](const std::string& text) -> std::string {
        std::ostringstream escaped;
        escaped << std::uppercase << std::hex << std::setfill('0');
        for (unsigned char byte : text) {
            if (byte >= 0x20 && byte <= 0x7e && byte != '"' && byte != '\\') {
                escaped << static_cast<char>(byte);
            } else {
                escaped << '\\' << std::setw(2) << static_cast<unsigned>(byte);
            }
        }
        return escaped.str();
    };
    std::size_t repeatedCopyLoad = 0;
    auto reloadRepeatedCopies = [&]() -> void {
        for (const auto& [id, type] : repeatedCopyTypes) {
            const std::string loaded = "%v" + std::to_string(id) + ".reload" + std::to_string(repeatedCopyLoad++);
            out << "  " << loaded << " = load " << llvmType(type) << ", ptr %copy" << id << "\n";
            values[id] = loaded;
        }
    };
    auto value = [&](ValueId id) -> std::string {
        if (const auto found = values.find(id); found != values.end()) return found->second;
        return "%v" + std::to_string(id);
    };
    auto extractStringPart = [&](const std::string& prefix, ValueId id, unsigned index) -> std::string {
        const ValueType type = program.valueTypes.at(id);
        if (type != ValueType::String && type != ValueType::StringView)
            throw std::runtime_error("backend LLVM: opération chaîne hors chaîne non supportée");
        const std::string part = prefix + (index == 0 ? ".ptr" : ".len");
        out << "  " << part << " = extractvalue " << llvmType(type) << " "
            << value(id) << ", " << index << "\n";
        return part;
    };
    auto pathsForValue = [&](ValueId id) -> HeapStringPaths {
        if (const auto found = heapStringValuePaths.find(id); found != heapStringValuePaths.end())
            return found->second;
        if (heapStringValues.contains(id)) return HeapStringPaths{HeapStringPath{}};
        return {};
    };
    auto rememberValuePaths = [&](ValueId id, const HeapStringPaths& paths) -> void {
        if (paths.empty()) {
            heapStringValuePaths.erase(id);
            if (program.valueTypes.at(id) == ValueType::String) heapStringValues.erase(id);
            return;
        }
        heapStringValuePaths[id] = paths;
        if (program.valueTypes.at(id) == ValueType::String && paths.contains(HeapStringPath{}))
            heapStringValues.insert(id);
    };
    auto prefixPaths = [](const HeapStringPaths& paths, std::size_t field) -> HeapStringPaths {
        HeapStringPaths prefixed;
        for (HeapStringPath path : paths) {
            path.insert(path.begin(), field);
            prefixed.insert(std::move(path));
        }
        return prefixed;
    };
    auto pathsForField = [](const HeapStringPaths& paths, std::size_t field) -> HeapStringPaths {
        HeapStringPaths selected;
        for (const HeapStringPath& path : paths) {
            if (!path.empty() && path.front() == field)
                selected.insert(HeapStringPath(path.begin() + 1, path.end()));
        }
        return selected;
    };
    auto removeFieldPaths = [](HeapStringPaths paths, std::size_t field) -> HeapStringPaths {
        for (auto it = paths.begin(); it != paths.end();) {
            if (!it->empty() && it->front() == field) {
                it = paths.erase(it);
            } else {
                ++it;
            }
        }
        return paths;
    };
    std::function<HeapStringPaths(const ValueType&)> heapStringPathsForType =
        [&](const ValueType& type) -> HeapStringPaths {
            if (type == ValueType::String) return HeapStringPaths{HeapStringPath{}};
            if (type.kind != ValueType::Kind::Struct) return {};
            HeapStringPaths paths;
            for (std::size_t i = 0; i < type.structure->fields.size(); ++i) {
                const HeapStringPaths fieldPaths = prefixPaths(
                    heapStringPathsForType(type.structure->fields[i].type), i);
                paths.insert(fieldPaths.begin(), fieldPaths.end());
            }
            return paths;
        };
    std::size_t ownershipSequence = 0;
    auto emitStringFree = [&](const std::string& prefix, const std::string& stringValue,
                              const ValueType& stringType) -> void {
        const std::string data = prefix + ".ptr";
        const std::string raw = prefix + ".raw";
        const std::string count = prefix + ".refcount";
        const std::string isStatic = prefix + ".is_static";
        const std::string decremented = prefix + ".decremented";
        const std::string shouldFree = prefix + ".should_free";
        const std::string labelStem = "string.drop." + std::to_string(ownershipSequence++);
        const std::string decrementLabel = labelStem + ".decrement";
        const std::string freeLabel = labelStem + ".free";
        const std::string doneLabel = labelStem + ".done";
        out << "  " << data << " = extractvalue " << llvmType(stringType) << " "
            << stringValue << ", 0\n"
            << "  " << raw << " = getelementptr i8, ptr " << data << ", i64 -16\n"
            << "  " << count << " = load i64, ptr " << raw << "\n"
            << "  " << isStatic << " = icmp eq i64 " << count << ", -1\n"
            << "  br i1 " << isStatic << ", label %" << doneLabel << ", label %" << decrementLabel << "\n"
            << decrementLabel << ":\n"
            << "  " << decremented << " = add i64 " << count << ", -1\n"
            << "  store i64 " << decremented << ", ptr " << raw << "\n"
            << "  " << shouldFree << " = icmp eq i64 " << decremented << ", 0\n"
            << "  br i1 " << shouldFree << ", label %" << freeLabel << ", label %" << doneLabel << "\n"
            << freeLabel << ":\n"
            << "  call void @free(ptr " << raw << ")\n"
            << "  br label %" << doneLabel << "\n"
            << doneLabel << ":\n";
    };
    auto emitStringRetain = [&](const std::string& prefix, const std::string& stringValue,
                                const ValueType& stringType) -> void {
        const std::string data = prefix + ".ptr";
        const std::string raw = prefix + ".raw";
        const std::string count = prefix + ".refcount";
        const std::string isStatic = prefix + ".is_static";
        const std::string retained = prefix + ".retained";
        const std::string labelStem = "string.retain." + std::to_string(ownershipSequence++);
        const std::string retainLabel = labelStem + ".retain";
        const std::string doneLabel = labelStem + ".done";
        out << "  " << data << " = extractvalue " << llvmType(stringType) << " "
            << stringValue << ", 0\n"
            << "  " << raw << " = getelementptr i8, ptr " << data << ", i64 -16\n"
            << "  " << count << " = load i64, ptr " << raw << "\n"
            << "  " << isStatic << " = icmp eq i64 " << count << ", -1\n"
            << "  br i1 " << isStatic << ", label %" << doneLabel << ", label %" << retainLabel << "\n"
            << retainLabel << ":\n"
            << "  " << retained << " = add i64 " << count << ", 1\n"
            << "  store i64 " << retained << ", ptr " << raw << "\n"
            << "  br label %" << doneLabel << "\n"
            << doneLabel << ":\n";
    };
    auto valueAtHeapStringPath = [&](const std::string& prefix, const std::string& rootValue,
                                     const ValueType& rootType,
                                     const HeapStringPath& path) -> std::pair<std::string, ValueType> {
        std::string currentValue = rootValue;
        ValueType currentType = rootType;
        for (std::size_t depth = 0; depth < path.size(); ++depth) {
            const std::size_t fieldIndex = path[depth];
            const std::string fieldValue = prefix + ".field" + std::to_string(depth);
            out << "  " << fieldValue << " = extractvalue " << llvmType(currentType) << " "
                << currentValue << ", " << fieldIndex << "\n";
            currentValue = fieldValue;
            currentType = currentType.structure->fields[fieldIndex].type;
        }
        return {currentValue, currentType};
    };
    auto emitHeapStringPathDrop = [&](const std::string& prefix, const std::string& rootValue,
                                      const ValueType& rootType, const HeapStringPath& path) -> void {
        const auto [currentValue, currentType] = valueAtHeapStringPath(prefix, rootValue, rootType, path);
        emitStringFree(prefix, currentValue, currentType);
    };
    auto emitHeapStringPathRetain = [&](const std::string& prefix, const std::string& rootValue,
                                        const ValueType& rootType, const HeapStringPath& path) -> void {
        const auto [currentValue, currentType] = valueAtHeapStringPath(prefix, rootValue, rootType, path);
        emitStringRetain(prefix, currentValue, currentType);
    };
    auto unsupported = [](const char* instruction) -> void {
        throw std::runtime_error(std::string("backend LLVM: instruction non supportée ") + instruction);
    };
    std::size_t syntheticBlock = 0;
    auto nextSyntheticPrefix = [&](const std::string& stem) -> std::string {
        return stem + std::to_string(syntheticBlock++);
    };
    auto emitStringDecodeAtByte = [&](const std::string& base, ValueId stringId, ValueId offsetId) -> std::string {
        const std::string prefix = nextSyntheticPrefix("utf8_decode");
        const std::string rangeLabel = prefix + ".range";
        const std::string byteLabel = prefix + ".byte";
        const std::string check2Label = prefix + ".check2";
        const std::string twoLabel = prefix + ".two";
        const std::string check3Label = prefix + ".check3";
        const std::string threeLabel = prefix + ".three";
        const std::string check4Label = prefix + ".check4";
        const std::string fourLabel = prefix + ".four";
        const std::string invalidLabel = prefix + ".invalid";
        const std::string doneLabel = prefix + ".done";
        const std::string ptr = extractStringPart(base + ".string", stringId, 0);
        const std::string len = extractStringPart(base + ".string", stringId, 1);
        const std::string off64 = base + ".offset64";
        const std::string nonNegative = base + ".offset_non_negative";
        const std::string beforeEnd = base + ".offset_before_end";
        const std::string inRange = base + ".offset_in_range";
        const std::string bytePtr = base + ".byte_ptr";
        const std::string b0raw = base + ".b0.raw";
        const std::string b0 = base + ".b0";
        const std::string ascii = base + ".ascii";
        const std::string ge2 = base + ".two_ge";
        const std::string le2 = base + ".two_le";
        const std::string is2 = base + ".two_kind";
        const std::string ge3 = base + ".three_ge";
        const std::string le3 = base + ".three_le";
        const std::string is3 = base + ".three_kind";
        const std::string ge4 = base + ".four_ge";
        const std::string le4 = base + ".four_le";
        const std::string is4 = base + ".four_kind";
        auto loadByte = [&](const std::string& name, int delta) -> std::string {
            const std::string deltaName = name + ".offset";
            const std::string ptrName = name + ".ptr";
            const std::string rawName = name + ".raw";
            out << "  " << deltaName << " = add i64 " << off64 << ", " << delta << "\n"
                << "  " << ptrName << " = getelementptr i8, ptr " << ptr << ", i64 " << deltaName << "\n"
                << "  " << rawName << " = load i8, ptr " << ptrName << "\n"
                << "  " << name << " = zext i8 " << rawName << " to i32\n";
            return name;
        };
        auto emitHasByte = [&](const std::string& name, int delta) -> std::string {
            const std::string needed = name + ".needed";
            out << "  " << needed << " = add i64 " << off64 << ", " << delta << "\n"
                << "  " << name << " = icmp slt i64 " << needed << ", " << len << "\n";
            return name;
        };
        auto emitContinuation = [&](const std::string& name, const std::string& byte) -> std::string {
            const std::string masked = name + ".masked";
            out << "  " << masked << " = and i32 " << byte << ", 192\n"
                << "  " << name << " = icmp eq i32 " << masked << ", 128\n";
            return name;
        };
        out << "  " << off64 << " = sext i32 " << value(offsetId) << " to i64\n"
            << "  " << nonNegative << " = icmp sge i32 " << value(offsetId) << ", 0\n"
            << "  " << beforeEnd << " = icmp slt i64 " << off64 << ", " << len << "\n"
            << "  " << inRange << " = and i1 " << nonNegative << ", " << beforeEnd << "\n"
            << "  br i1 " << inRange << ", label %" << rangeLabel << ", label %" << invalidLabel << "\n"
            << rangeLabel << ":\n"
            << "  " << bytePtr << " = getelementptr i8, ptr " << ptr << ", i64 " << off64 << "\n"
            << "  " << b0raw << " = load i8, ptr " << bytePtr << "\n"
            << "  " << b0 << " = zext i8 " << b0raw << " to i32\n"
            << "  " << ascii << " = icmp ult i32 " << b0 << ", 128\n"
            << "  br i1 " << ascii << ", label %" << byteLabel << ", label %" << check2Label << "\n"
            << byteLabel << ":\n"
            << "  br label %" << doneLabel << "\n"
            << check2Label << ":\n"
            << "  " << ge2 << " = icmp uge i32 " << b0 << ", 194\n"
            << "  " << le2 << " = icmp ule i32 " << b0 << ", 223\n"
            << "  " << is2 << " = and i1 " << ge2 << ", " << le2 << "\n";
        const std::string has1 = emitHasByte(base + ".has1", 1);
        const std::string b1 = loadByte(base + ".b1", 1);
        const std::string c1 = emitContinuation(base + ".b1_cont", b1);
        const std::string ok2a = base + ".two_ok_a";
        const std::string ok2 = base + ".two_ok";
        const std::string cp2hi = base + ".two_hi";
        const std::string cp2lo = base + ".two_lo";
        const std::string cp2 = base + ".two_cp";
        out << "  " << ok2a << " = and i1 " << is2 << ", " << has1 << "\n"
            << "  " << ok2 << " = and i1 " << ok2a << ", " << c1 << "\n"
            << "  br i1 " << ok2 << ", label %" << twoLabel << ", label %" << check3Label << "\n"
            << twoLabel << ":\n"
            << "  " << cp2hi << " = and i32 " << b0 << ", 31\n"
            << "  " << cp2hi << ".shifted = shl i32 " << cp2hi << ", 6\n"
            << "  " << cp2lo << " = and i32 " << b1 << ", 63\n"
            << "  " << cp2 << " = or i32 " << cp2hi << ".shifted, " << cp2lo << "\n"
            << "  br label %" << doneLabel << "\n"
            << check3Label << ":\n"
            << "  " << ge3 << " = icmp uge i32 " << b0 << ", 224\n"
            << "  " << le3 << " = icmp ule i32 " << b0 << ", 239\n"
            << "  " << is3 << " = and i1 " << ge3 << ", " << le3 << "\n";
        const std::string has2 = emitHasByte(base + ".has2", 2);
        const std::string b2 = loadByte(base + ".b2", 2);
        const std::string c2 = emitContinuation(base + ".b2_cont", b2);
        const std::string ok3a = base + ".three_ok_a";
        const std::string ok3b = base + ".three_ok_b";
        const std::string ok3 = base + ".three_ok";
        const std::string cp3a = base + ".three_a";
        const std::string cp3b = base + ".three_b";
        const std::string cp3c = base + ".three_c";
        const std::string cp3ab = base + ".three_ab";
        const std::string cp3 = base + ".three_cp";
        out << "  " << ok3a << " = and i1 " << is3 << ", " << has2 << "\n"
            << "  " << ok3b << " = and i1 " << c1 << ", " << c2 << "\n"
            << "  " << ok3 << " = and i1 " << ok3a << ", " << ok3b << "\n"
            << "  br i1 " << ok3 << ", label %" << threeLabel << ", label %" << check4Label << "\n"
            << threeLabel << ":\n"
            << "  " << cp3a << " = and i32 " << b0 << ", 15\n"
            << "  " << cp3a << ".shifted = shl i32 " << cp3a << ", 12\n"
            << "  " << cp3b << " = and i32 " << b1 << ", 63\n"
            << "  " << cp3b << ".shifted = shl i32 " << cp3b << ", 6\n"
            << "  " << cp3c << " = and i32 " << b2 << ", 63\n"
            << "  " << cp3ab << " = or i32 " << cp3a << ".shifted, " << cp3b << ".shifted\n"
            << "  " << cp3 << " = or i32 " << cp3ab << ", " << cp3c << "\n"
            << "  br label %" << doneLabel << "\n"
            << check4Label << ":\n"
            << "  " << ge4 << " = icmp uge i32 " << b0 << ", 240\n"
            << "  " << le4 << " = icmp ule i32 " << b0 << ", 244\n"
            << "  " << is4 << " = and i1 " << ge4 << ", " << le4 << "\n";
        const std::string has3 = emitHasByte(base + ".has3", 3);
        const std::string b3 = loadByte(base + ".b3", 3);
        const std::string c3 = emitContinuation(base + ".b3_cont", b3);
        const std::string ok4a = base + ".four_ok_a";
        const std::string ok4b = base + ".four_ok_b";
        const std::string ok4c = base + ".four_ok_c";
        const std::string ok4 = base + ".four_ok";
        const std::string cp4a = base + ".four_a";
        const std::string cp4b = base + ".four_b";
        const std::string cp4c = base + ".four_c";
        const std::string cp4d = base + ".four_d";
        const std::string cp4ab = base + ".four_ab";
        const std::string cp4abc = base + ".four_abc";
        const std::string cp4 = base + ".four_cp";
        out << "  " << ok4a << " = and i1 " << is4 << ", " << has3 << "\n"
            << "  " << ok4b << " = and i1 " << c1 << ", " << c2 << "\n"
            << "  " << ok4c << " = and i1 " << ok4b << ", " << c3 << "\n"
            << "  " << ok4 << " = and i1 " << ok4a << ", " << ok4c << "\n"
            << "  br i1 " << ok4 << ", label %" << fourLabel << ", label %" << invalidLabel << "\n"
            << fourLabel << ":\n"
            << "  " << cp4a << " = and i32 " << b0 << ", 7\n"
            << "  " << cp4a << ".shifted = shl i32 " << cp4a << ", 18\n"
            << "  " << cp4b << " = and i32 " << b1 << ", 63\n"
            << "  " << cp4b << ".shifted = shl i32 " << cp4b << ", 12\n"
            << "  " << cp4c << " = and i32 " << b2 << ", 63\n"
            << "  " << cp4c << ".shifted = shl i32 " << cp4c << ", 6\n"
            << "  " << cp4d << " = and i32 " << b3 << ", 63\n"
            << "  " << cp4ab << " = or i32 " << cp4a << ".shifted, " << cp4b << ".shifted\n"
            << "  " << cp4abc << " = or i32 " << cp4ab << ", " << cp4c << ".shifted\n"
            << "  " << cp4 << " = or i32 " << cp4abc << ", " << cp4d << "\n"
            << "  br label %" << doneLabel << "\n"
            << invalidLabel << ":\n"
            << "  br label %" << doneLabel << "\n"
            << doneLabel << ":\n"
            << "  " << base << " = phi i32 [ " << b0 << ", %" << byteLabel << " ], [ "
            << cp2 << ", %" << twoLabel << " ], [ " << cp3 << ", %" << threeLabel
            << " ], [ " << cp4 << ", %" << fourLabel << " ], [ -1, %" << invalidLabel << " ]\n";
        return base;
    };
    auto emitStringViewCall = [&](const IrCall& item) -> bool {
        if (item.function == "io__print" || item.function == "io__println") {
            if (item.arguments.size() != 1 || item.argumentTypes.size() != 1 ||
                item.argumentTypes[0] != ValueType::String || item.returnType != ValueType::Unit) {
                throw std::runtime_error("backend LLVM: signature io.print/println non supportée");
            }
            const std::string base = "%v" + std::to_string(item.output);
            const std::string data = extractStringPart(base + ".string", item.arguments[0], 0);
            const std::string length = extractStringPart(base + ".string", item.arguments[0], 1);
            const char* newline = item.function == "io__println" ? "true" : "false";
            out << "  call void @zeta_rt_io_write_string(ptr " << data << ", i64 "
                << length << ", i1 " << newline << ")\n";
            return true;
        }
        if (item.function == "io__printInt" || item.function == "io__printlnInt") {
            if (item.arguments.size() != 1 || item.argumentTypes.size() != 1 ||
                item.argumentTypes[0] != ValueType::Int || item.returnType != ValueType::Unit) {
                throw std::runtime_error("backend LLVM: signature io.printInt/printlnInt non supportée");
            }
            const char* format = item.function == "io__printlnInt" ? "@zeta.fmt.int.nl" : "@zeta.fmt.int";
            out << "  call i32 (ptr, ...) @printf(ptr " << format << ", i32 "
                << value(item.arguments[0]) << ")\n";
            return true;
        }
        if (item.function == "io__printByte" || item.function == "io__printlnByte") {
            if (item.arguments.size() != 1 || item.argumentTypes.size() != 1 ||
                item.argumentTypes[0] != ValueType::Byte || item.returnType != ValueType::Unit) {
                throw std::runtime_error("backend LLVM: signature io.printByte/printlnByte non supportée");
            }
            const std::string widened = "%v" + std::to_string(item.output) + ".byte_i32";
            const char* format = item.function == "io__printlnByte" ? "@zeta.fmt.byte.nl" : "@zeta.fmt.byte";
            out << "  " << widened << " = zext i8 " << value(item.arguments[0]) << " to i32\n"
                << "  call i32 (ptr, ...) @printf(ptr " << format << ", i32 " << widened << ")\n";
            return true;
        }
        if (item.function == "io__printDouble" || item.function == "io__printlnDouble") {
            if (item.arguments.size() != 1 || item.argumentTypes.size() != 1 ||
                item.argumentTypes[0] != ValueType::Double || item.returnType != ValueType::Unit) {
                throw std::runtime_error("backend LLVM: signature io.printDouble/printlnDouble non supportée");
            }
            const char* format = item.function == "io__printlnDouble" ? "@zeta.fmt.double.nl" : "@zeta.fmt.double";
            out << "  call i32 (ptr, ...) @printf(ptr " << format << ", double "
                << value(item.arguments[0]) << ")\n";
            return true;
        }
        if (item.function == "io__printChar" || item.function == "io__printlnChar") {
            if (item.arguments.size() != 1 || item.argumentTypes.size() != 1 ||
                item.argumentTypes[0] != ValueType::Char || item.returnType != ValueType::Unit) {
                throw std::runtime_error("backend LLVM: signature io.printChar/printlnChar non supportée");
            }
            const std::string prefix = "%v" + std::to_string(item.output) + ".char";
            const std::string code = value(item.arguments[0]);
            const std::string buffer = prefix + ".buf";
            const std::string ptr = prefix + ".ptr";
            const std::string one = prefix + ".one";
            const std::string two = prefix + ".two";
            const std::string three = prefix + ".three";
            const std::string lenTailWide = prefix + ".len.tail.wide";
            const std::string lenTail = prefix + ".len.tail";
            const std::string len = prefix + ".len";
            const std::string b0TwoShift = prefix + ".b0.two.shift";
            const std::string b0Two = prefix + ".b0.two";
            const std::string b0ThreeShift = prefix + ".b0.three.shift";
            const std::string b0Three = prefix + ".b0.three";
            const std::string b0FourShift = prefix + ".b0.four.shift";
            const std::string b0Four = prefix + ".b0.four";
            const std::string b0TailWide = prefix + ".b0.tail.wide";
            const std::string b0MultiTail = prefix + ".b0.multi.tail";
            const std::string b0 = prefix + ".b0";
            const std::string b1ShiftTwo = prefix + ".b1.shift.two";
            const std::string b1ShiftThree = prefix + ".b1.shift.three";
            const std::string b1ShiftFour = prefix + ".b1.shift.four";
            const std::string b1MaskedTwo = prefix + ".b1.masked.two";
            const std::string b1MaskedThree = prefix + ".b1.masked.three";
            const std::string b1MaskedFour = prefix + ".b1.masked.four";
            const std::string b1Two = prefix + ".b1.two";
            const std::string b1Three = prefix + ".b1.three";
            const std::string b1Four = prefix + ".b1.four";
            const std::string b1Tail = prefix + ".b1.tail";
            const std::string b1 = prefix + ".b1";
            const std::string b2Shift = prefix + ".b2.shift";
            const std::string b2MaskedThree = prefix + ".b2.masked.three";
            const std::string b2MaskedFour = prefix + ".b2.masked.four";
            const std::string b2Three = prefix + ".b2.three";
            const std::string b2Four = prefix + ".b2.four";
            const std::string b2 = prefix + ".b2";
            const std::string b3Masked = prefix + ".b3.masked";
            const std::string b3 = prefix + ".b3";
            const std::string b0i8 = prefix + ".b0.i8";
            const std::string b1i8 = prefix + ".b1.i8";
            const std::string b2i8 = prefix + ".b2.i8";
            const std::string b3i8 = prefix + ".b3.i8";
            const std::string p1 = prefix + ".ptr1";
            const std::string p2 = prefix + ".ptr2";
            const std::string p3 = prefix + ".ptr3";
            out << "  " << buffer << " = alloca [4 x i8], align 1\n"
                << "  " << ptr << " = getelementptr inbounds [4 x i8], ptr " << buffer << ", i64 0, i64 0\n"
                << "  " << one << " = icmp ule i32 " << code << ", 127\n"
                << "  " << two << " = icmp ule i32 " << code << ", 2047\n"
                << "  " << three << " = icmp ule i32 " << code << ", 65535\n"
                << "  " << lenTailWide << " = select i1 " << three << ", i64 3, i64 4\n"
                << "  " << lenTail << " = select i1 " << two << ", i64 2, i64 " << lenTailWide << "\n"
                << "  " << len << " = select i1 " << one << ", i64 1, i64 " << lenTail << "\n"
                << "  " << b0TwoShift << " = lshr i32 " << code << ", 6\n"
                << "  " << b0Two << " = or i32 " << b0TwoShift << ", 192\n"
                << "  " << b0ThreeShift << " = lshr i32 " << code << ", 12\n"
                << "  " << b0Three << " = or i32 " << b0ThreeShift << ", 224\n"
                << "  " << b0FourShift << " = lshr i32 " << code << ", 18\n"
                << "  " << b0Four << " = or i32 " << b0FourShift << ", 240\n"
                << "  " << b0TailWide << " = select i1 " << three << ", i32 " << b0Three << ", i32 " << b0Four << "\n"
                << "  " << b0MultiTail << " = select i1 " << two << ", i32 " << b0Two << ", i32 " << b0TailWide << "\n"
                << "  " << b0 << " = select i1 " << one << ", i32 " << code << ", i32 " << b0MultiTail << "\n"
                << "  " << b1ShiftTwo << " = lshr i32 " << code << ", 0\n"
                << "  " << b1ShiftThree << " = lshr i32 " << code << ", 6\n"
                << "  " << b1ShiftFour << " = lshr i32 " << code << ", 12\n"
                << "  " << b1MaskedTwo << " = and i32 " << b1ShiftTwo << ", 63\n"
                << "  " << b1MaskedThree << " = and i32 " << b1ShiftThree << ", 63\n"
                << "  " << b1MaskedFour << " = and i32 " << b1ShiftFour << ", 63\n"
                << "  " << b1Two << " = or i32 " << b1MaskedTwo << ", 128\n"
                << "  " << b1Three << " = or i32 " << b1MaskedThree << ", 128\n"
                << "  " << b1Four << " = or i32 " << b1MaskedFour << ", 128\n"
                << "  " << b1Tail << " = select i1 " << two << ", i32 " << b1Two << ", i32 " << b1Three << "\n"
                << "  " << b1 << " = select i1 " << three << ", i32 " << b1Tail << ", i32 " << b1Four << "\n"
                << "  " << b2Shift << " = lshr i32 " << code << ", 6\n"
                << "  " << b2MaskedThree << " = and i32 " << code << ", 63\n"
                << "  " << b2MaskedFour << " = and i32 " << b2Shift << ", 63\n"
                << "  " << b2Three << " = or i32 " << b2MaskedThree << ", 128\n"
                << "  " << b2Four << " = or i32 " << b2MaskedFour << ", 128\n"
                << "  " << b2 << " = select i1 " << three << ", i32 " << b2Three << ", i32 " << b2Four << "\n"
                << "  " << b3Masked << " = and i32 " << code << ", 63\n"
                << "  " << b3 << " = or i32 " << b3Masked << ", 128\n"
                << "  " << b0i8 << " = trunc i32 " << b0 << " to i8\n"
                << "  " << b1i8 << " = trunc i32 " << b1 << " to i8\n"
                << "  " << b2i8 << " = trunc i32 " << b2 << " to i8\n"
                << "  " << b3i8 << " = trunc i32 " << b3 << " to i8\n"
                << "  store i8 " << b0i8 << ", ptr " << ptr << ", align 1\n"
                << "  " << p1 << " = getelementptr i8, ptr " << ptr << ", i64 1\n"
                << "  store i8 " << b1i8 << ", ptr " << p1 << ", align 1\n"
                << "  " << p2 << " = getelementptr i8, ptr " << ptr << ", i64 2\n"
                << "  store i8 " << b2i8 << ", ptr " << p2 << ", align 1\n"
                << "  " << p3 << " = getelementptr i8, ptr " << ptr << ", i64 3\n"
                << "  store i8 " << b3i8 << ", ptr " << p3 << ", align 1\n"
                << "  call i64 @write(i32 1, ptr " << ptr << ", i64 " << len << ")\n";
            if (item.function == "io__printlnChar") {
                out << "  call i64 @write(i32 1, ptr @zeta.newline, i64 1)\n";
            }
            return true;
        }
        if (item.function == "io__printBool" || item.function == "io__printlnBool") {
            if (item.arguments.size() != 1 || item.argumentTypes.size() != 1 ||
                item.argumentTypes[0] != ValueType::Bool || item.returnType != ValueType::Unit) {
                throw std::runtime_error("backend LLVM: signature io.printBool/printlnBool non supportée");
            }
            const std::string base = "%v" + std::to_string(item.output);
            const std::string data = base + ".bool_data";
            const std::string length = base + ".bool_len";
            out << "  " << data << " = select i1 " << value(item.arguments[0])
                << ", ptr @zeta.bool.true, ptr @zeta.bool.false\n"
                << "  " << length << " = select i1 " << value(item.arguments[0])
                << ", i64 4, i64 5\n"
                << "  call i64 @write(i32 1, ptr " << data << ", i64 " << length << ")\n";
            if (item.function == "io__printlnBool") {
                out << "  call i64 @write(i32 1, ptr @zeta.newline, i64 1)\n";
            }
            return true;
        }
        if (item.function == "strings__view") {
            if (item.arguments.size() != 3 || item.argumentTypes.size() != 3 ||
                item.argumentTypes[0] != ValueType::String || item.argumentTypes[1] != ValueType::Int ||
                item.argumentTypes[2] != ValueType::Int || item.returnType != ValueType::StringView) {
                throw std::runtime_error("backend LLVM: signature strings.view non supportée");
            }
            const std::string base = "%v" + std::to_string(item.output);
            const std::string sourcePtr = extractStringPart(base + ".source", item.arguments[0], 0);
            const std::string sourceLen = extractStringPart(base + ".source", item.arguments[0], 1);
            const std::string start64 = base + ".start64";
            const std::string end64 = base + ".end64";
            const std::string startOk = base + ".start_ok";
            const std::string orderOk = base + ".order_ok";
            const std::string endOk = base + ".end_ok";
            const std::string validPrefix = base + ".valid_prefix";
            const std::string valid = base + ".valid";
            const std::string rawPtr = base + ".raw_ptr";
            const std::string rawLen = base + ".raw_len";
            const std::string viewPtr = base + ".ptr";
            const std::string viewLen = base + ".len";
            const std::string pairPtr = base + ".pair_ptr";
            out << "  " << start64 << " = sext i32 " << value(item.arguments[1]) << " to i64\n"
                << "  " << end64 << " = sext i32 " << value(item.arguments[2]) << " to i64\n"
                << "  " << startOk << " = icmp sge i32 " << value(item.arguments[1]) << ", 0\n"
                << "  " << orderOk << " = icmp sle i32 " << value(item.arguments[1]) << ", "
                << value(item.arguments[2]) << "\n"
                << "  " << endOk << " = icmp sle i64 " << end64 << ", " << sourceLen << "\n"
                << "  " << validPrefix << " = and i1 " << startOk << ", " << orderOk << "\n"
                << "  " << valid << " = and i1 " << validPrefix << ", " << endOk << "\n"
                << "  " << rawPtr << " = getelementptr i8, ptr " << sourcePtr << ", i64 " << start64 << "\n"
                << "  " << rawLen << " = sub i64 " << end64 << ", " << start64 << "\n"
                << "  " << viewPtr << " = select i1 " << valid << ", ptr " << rawPtr << ", ptr null\n"
                << "  " << viewLen << " = select i1 " << valid << ", i64 " << rawLen << ", i64 0\n"
                << "  " << pairPtr << " = insertvalue { ptr, i64 } undef, ptr " << viewPtr << ", 0\n"
                << "  " << base << " = insertvalue { ptr, i64 } " << pairPtr << ", i64 " << viewLen << ", 1\n";
            values[item.output] = base;
            return true;
        }
        if (item.function == "strings__viewIsValid") {
            if (item.arguments.size() != 1 || item.argumentTypes.size() != 1 ||
                item.argumentTypes[0] != ValueType::StringView || item.returnType != ValueType::Bool) {
                throw std::runtime_error("backend LLVM: signature strings.viewIsValid non supportée");
            }
            const std::string output = "%v" + std::to_string(item.output);
            const std::string ptr = extractStringPart(output, item.arguments[0], 0);
            out << "  " << output << " = icmp ne ptr " << ptr << ", null\n";
            values[item.output] = output;
            return true;
        }
        auto emitStringIndexOf = [&](const std::string& base, ValueId haystackId, ValueId needleId) -> std::string {
            const std::string prefix = nextSyntheticPrefix("string_search");
            const std::string invalidLabel = prefix + ".invalid";
            const std::string rangeLabel = prefix + ".range";
            const std::string emptyLabel = prefix + ".empty";
            const std::string loopLabel = prefix + ".loop";
            const std::string compareLabel = prefix + ".compare";
            const std::string memcmpLabel = prefix + ".memcmp";
            const std::string foundLabel = prefix + ".found";
            const std::string nextLabel = prefix + ".next";
            const std::string doneLabel = prefix + ".done";
            const std::string hayPtr = extractStringPart(base + ".haystack", haystackId, 0);
            const std::string hayLen = extractStringPart(base + ".haystack", haystackId, 1);
            const std::string needlePtr = extractStringPart(base + ".needle", needleId, 0);
            const std::string needleLen = extractStringPart(base + ".needle", needleId, 1);
            const std::string hayValid = base + ".haystack_valid";
            const std::string needleValid = base + ".needle_valid";
            const std::string valid = base + ".valid";
            const std::string isEmpty = base + ".empty";
            const std::string fits = base + ".fits";
            const std::string limit = base + ".limit";
            const std::string index = base + ".index";
            const std::string inRange = base + ".in_range";
            const std::string candidate = base + ".candidate";
            const std::string cmp = base + ".cmp";
            const std::string match = base + ".match";
            const std::string found32 = base + ".found32";
            const std::string next = base + ".next";
            out << "  " << hayValid << " = icmp ne ptr " << hayPtr << ", null\n"
                << "  " << needleValid << " = icmp ne ptr " << needlePtr << ", null\n"
                << "  " << valid << " = and i1 " << hayValid << ", " << needleValid << "\n"
                << "  br i1 " << valid << ", label %" << rangeLabel << ", label %" << invalidLabel << "\n"
                << invalidLabel << ":\n"
                << "  br label %" << doneLabel << "\n"
                << rangeLabel << ":\n"
                << "  " << isEmpty << " = icmp eq i64 " << needleLen << ", 0\n"
                << "  br i1 " << isEmpty << ", label %" << emptyLabel << ", label %" << loopLabel << "\n"
                << emptyLabel << ":\n"
                << "  br label %" << doneLabel << "\n"
                << loopLabel << ":\n"
                << "  " << fits << " = icmp ule i64 " << needleLen << ", " << hayLen << "\n"
                << "  " << limit << " = sub i64 " << hayLen << ", " << needleLen << "\n"
                << "  br i1 " << fits << ", label %" << compareLabel << ", label %" << invalidLabel << "\n"
                << compareLabel << ":\n"
                << "  " << index << " = phi i64 [ 0, %" << loopLabel << " ], [ " << next << ", %" << nextLabel << " ]\n"
                << "  " << inRange << " = icmp ule i64 " << index << ", " << limit << "\n"
                << "  br i1 " << inRange << ", label %" << memcmpLabel << ", label %" << invalidLabel << "\n"
                << memcmpLabel << ":\n"
                << "  " << candidate << " = getelementptr i8, ptr " << hayPtr << ", i64 " << index << "\n"
                << "  " << cmp << " = call i32 @memcmp(ptr " << candidate << ", ptr " << needlePtr
                << ", i64 " << needleLen << ")\n"
                << "  " << match << " = icmp eq i32 " << cmp << ", 0\n"
                << "  br i1 " << match << ", label %" << foundLabel << ", label %" << nextLabel << "\n"
                << foundLabel << ":\n"
                << "  " << found32 << " = trunc i64 " << index << " to i32\n"
                << "  br label %" << doneLabel << "\n"
                << nextLabel << ":\n"
                << "  " << next << " = add i64 " << index << ", 1\n"
                << "  br label %" << compareLabel << "\n"
                << doneLabel << ":\n"
                << "  " << base << " = phi i32 [ -1, %" << invalidLabel << " ], [ 0, %"
                << emptyLabel << " ], [ " << found32 << ", %" << foundLabel << " ]\n";
            return base;
        };
        if (item.function == "strings__decodeAtByte") {
            if (item.arguments.size() != 2 || item.argumentTypes.size() != 2 ||
                (item.argumentTypes[0] != ValueType::String && item.argumentTypes[0] != ValueType::StringView) ||
                item.argumentTypes[1] != ValueType::Int ||
                item.returnType != ValueType::Int) {
                throw std::runtime_error("backend LLVM: signature strings.decodeAtByte non supportée");
            }
            const std::string output = "%v" + std::to_string(item.output);
            values[item.output] = emitStringDecodeAtByte(output, item.arguments[0], item.arguments[1]);
            return true;
        }
        if (item.function == "strings__nextByteOffset") {
            if (item.arguments.size() != 2 || item.argumentTypes.size() != 2 ||
                (item.argumentTypes[0] != ValueType::String && item.argumentTypes[0] != ValueType::StringView) ||
                item.argumentTypes[1] != ValueType::Int ||
                item.returnType != ValueType::Int) {
                throw std::runtime_error("backend LLVM: signature strings.nextByteOffset non supportée");
            }
            const std::string decoded = emitStringDecodeAtByte("%v" + std::to_string(item.output) + ".decoded",
                item.arguments[0], item.arguments[1]);
            const std::string output = "%v" + std::to_string(item.output);
            const std::string invalid = output + ".invalid";
            const std::string one = output + ".one";
            const std::string two = output + ".two";
            const std::string three = output + ".three";
            const std::string plus1 = output + ".plus1";
            const std::string plus2 = output + ".plus2";
            const std::string plus3 = output + ".plus3";
            const std::string plus4 = output + ".plus4";
            const std::string tail3 = output + ".tail3";
            const std::string tail2 = output + ".tail2";
            const std::string tail1 = output + ".tail1";
            out << "  " << invalid << " = icmp slt i32 " << decoded << ", 0\n"
                << "  " << one << " = icmp ule i32 " << decoded << ", 127\n"
                << "  " << two << " = icmp ule i32 " << decoded << ", 2047\n"
                << "  " << three << " = icmp ule i32 " << decoded << ", 65535\n"
                << "  " << plus1 << " = add i32 " << value(item.arguments[1]) << ", 1\n"
                << "  " << plus2 << " = add i32 " << value(item.arguments[1]) << ", 2\n"
                << "  " << plus3 << " = add i32 " << value(item.arguments[1]) << ", 3\n"
                << "  " << plus4 << " = add i32 " << value(item.arguments[1]) << ", 4\n"
                << "  " << tail3 << " = select i1 " << three << ", i32 " << plus3 << ", i32 " << plus4 << "\n"
                << "  " << tail2 << " = select i1 " << two << ", i32 " << plus2 << ", i32 " << tail3 << "\n"
                << "  " << tail1 << " = select i1 " << one << ", i32 " << plus1 << ", i32 " << tail2 << "\n"
                << "  " << output << " = select i1 " << invalid << ", i32 -1, i32 " << tail1 << "\n";
            values[item.output] = output;
            return true;
        }
        if (item.function == "strings__indexOf") {
            if (item.arguments.size() != 2 || item.argumentTypes.size() != 2 ||
                item.argumentTypes[0] != ValueType::StringView || item.argumentTypes[1] != ValueType::StringView ||
                item.returnType != ValueType::Int) {
                throw std::runtime_error("backend LLVM: signature strings.indexOf non supportée");
            }
            const std::string output = "%v" + std::to_string(item.output);
            values[item.output] = emitStringIndexOf(output, item.arguments[0], item.arguments[1]);
            return true;
        }
        if (item.function == "strings__contains") {
            if (item.arguments.size() != 2 || item.argumentTypes.size() != 2 ||
                item.argumentTypes[0] != ValueType::StringView || item.argumentTypes[1] != ValueType::StringView ||
                item.returnType != ValueType::Bool) {
                throw std::runtime_error("backend LLVM: signature strings.contains non supportée");
            }
            const std::string index = emitStringIndexOf("%v" + std::to_string(item.output) + ".index",
                item.arguments[0], item.arguments[1]);
            const std::string output = "%v" + std::to_string(item.output);
            out << "  " << output << " = icmp sge i32 " << index << ", 0\n";
            values[item.output] = output;
            return true;
        }
        return false;
    };
    auto slotName = [&](SlotId id) -> std::string {
        const IrSlot& slot = program.slots.at(id);
        return std::string(slot.global || slot.external ? "@slot" : "%slot") + std::to_string(id);
    };
    auto diagnosticSlotName = [](const IrSlot& slot, SlotId id) -> std::string {
        if (slot.name.empty()) return "slot" + std::to_string(id);
        const std::size_t separator = slot.name.rfind("__");
        if (separator != std::string::npos && separator + 2 < slot.name.size())
            return slot.name.substr(separator + 2);
        return slot.name;
    };
    auto labelName = [](std::size_t id) -> std::string {
        return "label" + std::to_string(id);
    };
    auto emitScalarAllocas = [&]() -> void {
        for (SlotId id = 0; id < program.slots.size(); ++id) {
            const IrSlot& slot = program.slots[id];
            if (slot.global || slot.external) continue;
            if (!isLlvmValueType(slot.type)) {
                const std::string category = isLlvmUnsupportedAggregate(slot.type)
                    ? "agrégat local non supporté " : "slot local non supporté ";
                throw std::runtime_error("backend LLVM: " + category +
                                         diagnosticSlotName(slot, id) + ": " + typeName(slot.type));
            }
            out << "  " << slotName(id) << " = alloca " << llvmType(slot.type) << "\n";
        }
        for (const auto& [id, type] : repeatedCopyTypes) {
            if (!isLlvmValueType(type))
                throw std::runtime_error("backend LLVM: copie SSA répétée non supportée " + typeName(type));
            out << "  %copy" << id << " = alloca " << llvmType(type) << "\n";
        }
    };

    const bool usesStringConcat = std::any_of(program.instructions.begin(), program.instructions.end(),
        [](const IrInstruction& instruction) { return std::holds_alternative<IrStringConcat>(instruction); });
    const bool usesStringOwnership = std::any_of(program.instructions.begin(), program.instructions.end(),
        [&](const IrInstruction& instruction) {
            return std::visit([&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, IrDrop> || std::is_same_v<T, IrRetain>)
                    return !heapStringPathsForType(item.type).empty();
                return false;
            }, instruction);
        });
    const bool usesStringSearch = std::any_of(program.instructions.begin(), program.instructions.end(),
        [](const IrInstruction& instruction) {
            if (const auto* call = std::get_if<IrCall>(&instruction))
                return call->function == "strings__indexOf" || call->function == "strings__contains";
            return false;
        });
    const bool usesIoStringWrite = std::any_of(program.instructions.begin(), program.instructions.end(),
        [](const IrInstruction& instruction) {
            if (const auto* call = std::get_if<IrCall>(&instruction))
                return call->function == "io__print" || call->function == "io__println";
            return false;
        });
    const bool usesIoWrite = std::any_of(program.instructions.begin(), program.instructions.end(),
        [](const IrInstruction& instruction) {
            if (const auto* call = std::get_if<IrCall>(&instruction))
                return call->function == "io__print" || call->function == "io__println" ||
                       call->function == "io__printChar" || call->function == "io__printlnChar" ||
                       call->function == "io__printBool" || call->function == "io__printlnBool";
            return false;
        });
    const bool usesIoPrintf = std::any_of(program.instructions.begin(), program.instructions.end(),
        [](const IrInstruction& instruction) {
            if (const auto* call = std::get_if<IrCall>(&instruction))
                return call->function == "io__printInt" || call->function == "io__printlnInt" ||
                       call->function == "io__printByte" || call->function == "io__printlnByte" ||
                       call->function == "io__printDouble" || call->function == "io__printlnDouble";
            return false;
        });
    const bool usesIoBoolWrite = std::any_of(program.instructions.begin(), program.instructions.end(),
        [](const IrInstruction& instruction) {
            if (const auto* call = std::get_if<IrCall>(&instruction))
                return call->function == "io__printBool" || call->function == "io__printlnBool";
            return false;
        });

    out << "target triple = \"x86_64-pc-linux-gnu\"\n\n";
    if (usesStringConcat) {
        out << "declare ptr @malloc(i64)\n"
            << "declare ptr @memcpy(ptr, ptr, i64)\n\n";
    }
    if (usesStringConcat || usesStringOwnership) {
        out << "declare void @free(ptr)\n\n";
    }
    if (usesStringSearch) {
        out << "declare i32 @memcmp(ptr, ptr, i64)\n\n";
    }
    if (usesIoWrite) {
        out << "declare i64 @write(i32, ptr, i64)\n\n";
    }
    if (usesIoPrintf) {
        out << "declare i32 @printf(ptr, ...)\n\n";
    }
    for (const IrInstruction& instruction : program.instructions) {
        if (const auto* item = std::get_if<IrStringConst>(&instruction)) {
            out << "@str." << item->output << " = private unnamed_addr constant { i64, i64, ["
                << item->utf8.size() << " x i8] } { i64 -1, i64 " << item->utf8.size()
                << ", [" << item->utf8.size() << " x i8] c\"" << llvmStringBytes(item->utf8)
                << "\" }, align 8\n";
        }
    }
    if (usesIoWrite) {
        out << "@zeta.newline = private unnamed_addr constant [1 x i8] c\"\\0A\", align 1\n";
    }
    if (usesIoBoolWrite) {
        out << "@zeta.bool.true = private unnamed_addr constant [4 x i8] c\"true\", align 1\n"
            << "@zeta.bool.false = private unnamed_addr constant [5 x i8] c\"false\", align 1\n";
    }
    if (usesIoPrintf) {
        out << "@zeta.fmt.int = private unnamed_addr constant [3 x i8] c\"%d\\00\", align 1\n"
            << "@zeta.fmt.int.nl = private unnamed_addr constant [4 x i8] c\"%d\\0A\\00\", align 1\n"
            << "@zeta.fmt.byte = private unnamed_addr constant [3 x i8] c\"%u\\00\", align 1\n"
            << "@zeta.fmt.byte.nl = private unnamed_addr constant [4 x i8] c\"%u\\0A\\00\", align 1\n"
            << "@zeta.fmt.double = private unnamed_addr constant [3 x i8] c\"%g\\00\", align 1\n"
            << "@zeta.fmt.double.nl = private unnamed_addr constant [4 x i8] c\"%g\\0A\\00\", align 1\n";
    }
    if (usesIoStringWrite) {
        out << "\ndefine internal void @zeta_rt_io_write_string(ptr %data, i64 %len, i1 %newline) {\n"
            << "entry:\n"
            << "  call i64 @write(i32 1, ptr %data, i64 %len)\n"
            << "  br i1 %newline, label %write_newline, label %done\n"
            << "write_newline:\n"
            << "  call i64 @write(i32 1, ptr @zeta.newline, i64 1)\n"
            << "  br label %done\n"
            << "done:\n"
            << "  ret void\n"
            << "}\n";
    }
    for (SlotId id = 0; id < program.slots.size(); ++id) {
        const IrSlot& slot = program.slots[id];
        if (!slot.global && !slot.external) continue;
        if (slot.type != ValueType::Int && slot.type != ValueType::Bool) {
            const bool globalAggregate = slot.type.kind == ValueType::Kind::Array ||
                slot.type.kind == ValueType::Kind::Slice || slot.type.kind == ValueType::Kind::Box ||
                slot.type.kind == ValueType::Kind::Vec || slot.type.kind == ValueType::Kind::Struct ||
                slot.type.kind == ValueType::Kind::Enum;
            const std::string category = globalAggregate
                ? "agrégat global non supporté " : "globale non scalaire non supportée ";
            throw std::runtime_error("backend LLVM: " + category +
                                     diagnosticSlotName(slot, id) + ": " + typeName(slot.type));
        }
        out << slotName(id) << " = ";
        if (slot.external) {
            out << "external global " << llvmType(slot.type) << "\n";
        } else {
            out << "global " << llvmType(slot.type) << " 0\n";
        }
    }
    out << "\ndefine i32 @main() {\nentry:\n";
    emitScalarAllocas();
    bool openFunction = true;
    bool terminated = false;
    bool skippingFunction = false;
    for (const IrInstruction& instruction : program.instructions) {
        if (const auto* item = std::get_if<IrFunctionStart>(&instruction)) {
            if (openFunction) {
                if (!terminated) out << "  ret i32 0\n";
                out << "}\n\n";
            }
            const bool unreachableImportedStringsFunction =
                (!reachableFunctions.contains(item->name) || item->name == "strings__contains" ||
                 item->name == "strings__nextByteOffset") &&
                item->name.rfind("strings__", 0) == 0;
            const bool unreachableImportedIoFunction =
                (!reachableFunctions.contains(item->name) || item->name == "io__printInt" ||
                 item->name == "io__printlnInt" || item->name == "io__printByte" ||
                 item->name == "io__printlnByte" || item->name == "io__printChar" ||
                 item->name == "io__printlnChar" || item->name == "io__printDouble" ||
                 item->name == "io__printlnDouble" || item->name == "io__printBool" ||
                 item->name == "io__printlnBool") &&
                item->name.rfind("io__", 0) == 0;
            if (unreachableImportedStringsFunction || unreachableImportedIoFunction) {
                skippingFunction = true;
                openFunction = false;
                terminated = true;
                continue;
            }
            skippingFunction = false;
            const ValueType returnType = functionReturnTypes.contains(item->name)
                ? functionReturnTypes.at(item->name) : ValueType::Int;
            out << "define " << llvmType(returnType) << " @" << item->name << "(";
            const std::vector<ValueType>& parameters = functionParameterTypes[item->name];
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                if (i != 0) out << ", ";
                out << llvmType(parameters[i]) << " %arg" << i;
            }
            out << ") {\nentry:\n";
            emitScalarAllocas();
            openFunction = true;
            terminated = false;
            continue;
        }
        if (skippingFunction) continue;
        if (const auto* item = std::get_if<IrConst>(&instruction)) {
            if (item->type != ValueType::Int && item->type != ValueType::Byte &&
                item->type != ValueType::Bool && item->type != ValueType::Char)
                throw std::runtime_error("backend LLVM: type non supporté " + typeName(item->type));
            values[item->output] = std::to_string(item->value);
        } else if (const auto* item = std::get_if<IrStringConst>(&instruction)) {
            const std::string global = "@str." + std::to_string(item->output);
            values[item->output] = "{ ptr getelementptr inbounds ({ i64, i64, [" +
                std::to_string(item->utf8.size()) + " x i8] }, ptr " + global +
                ", i64 0, i32 2, i64 0), i64 " + std::to_string(item->utf8.size()) + " }";
            rememberValuePaths(item->output, HeapStringPaths{HeapStringPath{}});
        } else if (const auto* item = std::get_if<IrDoubleConst>(&instruction)) {
            values[item->output] = formatDouble(item->value);
        } else if (const auto* item = std::get_if<IrCall>(&instruction)) {
            if (emitStringViewCall(*item)) continue;
            const std::string returnType = llvmType(item->returnType);
            out << "  %v" << item->output << " = call " << returnType << " @"
                << item->function << "(";
            for (std::size_t i = 0; i < item->arguments.size(); ++i) {
                if (i != 0) out << ", ";
                out << llvmType(item->argumentTypes.at(i)) << " " << value(item->arguments[i]);
            }
            out << ")\n";
            values[item->output] = "%v" + std::to_string(item->output);
            rememberValuePaths(item->output, heapStringPathsForType(item->returnType));
        } else if (const auto* item = std::get_if<IrExit>(&instruction)) {
            const ValueType type = program.valueTypes.at(item->value);
            if (type != ValueType::Int)
                throw std::runtime_error("backend LLVM: type de sortie non supporté " + typeName(type));
            out << "  ret i32 " << value(item->value) << "\n";
            terminated = true;
        } else if (const auto* item = std::get_if<IrFunctionStart>(&instruction)) {
            if (openFunction) {
                if (!terminated) out << "  ret i32 0\n";
                out << "}\n\n";
            }
            const ValueType returnType = functionReturnTypes.contains(item->name)
                ? functionReturnTypes.at(item->name) : ValueType::Int;
            out << "define " << llvmType(returnType) << " @" << item->name << "(";
            const std::vector<ValueType>& parameters = functionParameterTypes[item->name];
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                if (i != 0) out << ", ";
                out << llvmType(parameters[i]) << " %arg" << i;
            }
            out << ") {\nentry:\n";
            emitScalarAllocas();
            openFunction = true;
            terminated = false;
        } else if (const auto* item = std::get_if<IrParameter>(&instruction)) {
            if (!isLlvmValueType(item->type))
                throw std::runtime_error("backend LLVM: paramètre non supporté " + typeName(item->type));
            values[item->output] = "%arg" + std::to_string(item->index);
            rememberValuePaths(item->output, heapStringPathsForType(item->type));
        } else if (const auto* item = std::get_if<IrReturn>(&instruction)) {
            if (!isLlvmValueType(item->type))
                throw std::runtime_error("backend LLVM: type de retour non supporté " + typeName(item->type));
            out << "  ret " << llvmType(item->type) << " " << value(item->value) << "\n";
            terminated = true;
        } else if (std::holds_alternative<IrUnit>(instruction)) {
            unsupported("unit");
        } else if (const auto* item = std::get_if<IrLabel>(&instruction)) {
            if (!terminated) out << "  br label %" << labelName(item->label) << "\n";
            out << labelName(item->label) << ":\n";
            reloadRepeatedCopies();
            terminated = false;
        } else if (const auto* item = std::get_if<IrJump>(&instruction)) {
            out << "  br label %" << labelName(item->label) << "\n";
            terminated = true;
        } else if (const auto* item = std::get_if<IrBranch>(&instruction)) {
            if (program.valueTypes.at(item->condition) != ValueType::Bool)
                throw std::runtime_error("backend LLVM: condition de branche non booléenne");
            const std::string fallthrough = "bb" + std::to_string(syntheticBlock++);
            out << "  br i1 " << value(item->condition) << ", label %";
            if (item->jumpWhenTrue) {
                out << labelName(item->label) << ", label %" << fallthrough << "\n";
            } else {
                out << fallthrough << ", label %" << labelName(item->label) << "\n";
            }
            out << fallthrough << ":\n";
            terminated = false;
        } else if (const auto* item = std::get_if<IrBinary>(&instruction)) {
            if (item->operandType != ValueType::Int && item->operandType != ValueType::Byte &&
                item->operandType != ValueType::Bool && item->operandType != ValueType::Char &&
                item->operandType != ValueType::Double)
                throw std::runtime_error("backend LLVM: type opérande non supporté " +
                                         typeName(item->operandType));
            const std::string output = "%v" + std::to_string(item->output);
            if (item->op == "+" || item->op == "-" || item->op == "*" || item->op == "/") {
                if (item->type == ValueType::Int && item->operandType == ValueType::Int) {
                    const char* operation = item->op == "+" ? "add nsw" :
                        item->op == "-" ? "sub nsw" : item->op == "*" ? "mul nsw" : "sdiv";
                    out << "  " << output << " = " << operation << " i32 "
                        << value(item->left) << ", " << value(item->right) << "\n";
                } else if (item->type == ValueType::Double && item->operandType == ValueType::Double) {
                    const char* operation = item->op == "+" ? "fadd" :
                        item->op == "-" ? "fsub" : item->op == "*" ? "fmul" : "fdiv";
                    out << "  " << output << " = " << operation << " double "
                        << value(item->left) << ", " << value(item->right) << "\n";
                } else {
                    throw std::runtime_error("backend LLVM: opération scalaire non supportée " + item->op);
                }
                values[item->output] = output;
            } else if (item->op == "==" || item->op == "!=" || item->op == "<" ||
                       item->op == "<=" || item->op == ">" || item->op == ">=") {
                if (item->type != ValueType::Bool)
                    throw std::runtime_error("backend LLVM: comparaison non booléenne non supportée");
                if (item->operandType == ValueType::Double) {
                    const char* predicate = item->op == "==" ? "oeq" : item->op == "!=" ? "one" :
                        item->op == "<" ? "olt" : item->op == "<=" ? "ole" :
                        item->op == ">" ? "ogt" : "oge";
                    out << "  " << output << " = fcmp " << predicate << " double "
                        << value(item->left) << ", " << value(item->right) << "\n";
                } else {
                    const char* predicate = item->op == "==" ? "eq" : item->op == "!=" ? "ne" :
                        item->op == "<" ? "slt" : item->op == "<=" ? "sle" :
                        item->op == ">" ? "sgt" : "sge";
                    out << "  " << output << " = icmp " << predicate << " "
                        << llvmType(item->operandType) << " " << value(item->left) << ", "
                        << value(item->right) << "\n";
                }
                values[item->output] = output;
            } else if (item->op == "&&" || item->op == "||") {
                if (item->type != ValueType::Bool || item->operandType != ValueType::Bool)
                    throw std::runtime_error("backend LLVM: opération booléenne non supportée " + item->op);
                out << "  " << output << " = " << (item->op == "&&" ? "and" : "or")
                    << " i1 " << value(item->left) << ", " << value(item->right) << "\n";
                values[item->output] = output;
            } else {
                unsupported("binary");
            }
        } else if (const auto* item = std::get_if<IrUnary>(&instruction)) {
            const std::string output = "%v" + std::to_string(item->output);
            if (item->type == ValueType::Double && item->op == "-") {
                out << "  " << output << " = fneg double " << value(item->operand) << "\n";
                values[item->output] = output;
            } else {
                unsupported("unary");
            }
        } else if (const auto* item = std::get_if<IrCopy>(&instruction)) {
            if (item->type == ValueType::Unit) {
                values[item->output] = "0";
            } else if (item->type == ValueType::Int || item->type == ValueType::Byte ||
                       item->type == ValueType::Bool || item->type == ValueType::Char ||
                       item->type == ValueType::Double ||
                       item->type.kind == ValueType::Kind::Struct) {
                if (repeatedCopyTypes.contains(item->output)) {
                    const std::string input = value(item->input);
                    out << "  store " << llvmType(item->type) << " " << input
                        << ", ptr %copy" << item->output << "\n";
                    values[item->output] = input;
                } else {
                    values[item->output] = value(item->input);
                }
                rememberValuePaths(item->output, pathsForValue(item->input));
            } else {
                throw std::runtime_error("backend LLVM: type de copie non supporté " +
                                         typeName(item->type));
            }
        } else if (const auto* item = std::get_if<IrConvert>(&instruction)) {
            if ((item->source == ValueType::Char && item->target == ValueType::Int) ||
                (item->source == ValueType::Int && item->target == ValueType::Char)) {
                values[item->output] = value(item->input);
            } else if (item->source == ValueType::Int && item->target == ValueType::Byte) {
                const std::string output = "%v" + std::to_string(item->output);
                out << "  " << output << " = trunc i32 " << value(item->input) << " to i8\n";
                values[item->output] = output;
            } else if (item->source == ValueType::Byte && item->target == ValueType::Int) {
                const std::string output = "%v" + std::to_string(item->output);
                out << "  " << output << " = zext i8 " << value(item->input) << " to i32\n";
                values[item->output] = output;
            } else {
                throw std::runtime_error("backend LLVM: conversion non supportée " +
                                         typeName(item->source) + " vers " + typeName(item->target));
            }
        } else if (const auto* item = std::get_if<IrStructConstruct>(&instruction)) {
            if (!isLlvmValueType(item->type) || item->type.kind != ValueType::Kind::Struct)
                throw std::runtime_error("backend LLVM: construction struct non supportée " + typeName(item->type));
            const std::string structType = llvmType(item->type);
            std::string current = "undef";
            for (std::size_t i = 0; i < item->fields.size(); ++i) {
                const std::string output = "%v" + std::to_string(item->output) + ".field" + std::to_string(i);
                const ValueType& fieldType = item->type.structure->fields[i].type;
                out << "  " << output << " = insertvalue " << structType << " " << current
                    << ", " << llvmType(fieldType) << " " << value(item->fields[i]) << ", " << i << "\n";
                current = output;
            }
            values[item->output] = current;
            HeapStringPaths structPaths;
            for (std::size_t i = 0; i < item->fields.size(); ++i) {
                const HeapStringPaths fieldPaths = prefixPaths(pathsForValue(item->fields[i]), i);
                structPaths.insert(fieldPaths.begin(), fieldPaths.end());
            }
            rememberValuePaths(item->output, structPaths);
        } else if (const auto* item = std::get_if<IrFieldLoad>(&instruction)) {
            if (!isLlvmValueType(item->objectType) || item->objectType.kind != ValueType::Kind::Struct)
                throw std::runtime_error("backend LLVM: chargement champ struct non supporté " +
                                         typeName(item->objectType));
            const std::string output = "%v" + std::to_string(item->output);
            out << "  " << output << " = extractvalue " << llvmType(item->objectType) << " "
                << value(item->object) << ", " << item->field << "\n";
            values[item->output] = output;
            rememberValuePaths(item->output, pathsForField(pathsForValue(item->object), item->field));
        } else if (const auto* item = std::get_if<IrFieldStore>(&instruction)) {
            if (!isLlvmValueType(item->objectType) || item->objectType.kind != ValueType::Kind::Struct)
                throw std::runtime_error("backend LLVM: mutation champ struct non supportée " +
                                         typeName(item->objectType));
            const StructField& field = item->objectType.structure->fields[item->field];
            if (!isLlvmValueType(field.type))
                throw std::runtime_error("backend LLVM: mutation champ struct non supportée " +
                                         typeName(item->objectType));
            const std::string base = "%slot" + std::to_string(item->slot) + ".field" +
                                     std::to_string(item->field) + ".store" +
                                     std::to_string(ownershipSequence++);
            const std::string current = base + ".load";
            const std::string updated = base + ".updated";
            const std::string structType = llvmType(item->objectType);
            out << "  " << current << " = load " << structType << ", ptr " << slotName(item->slot) << "\n";
            std::size_t replacedPathIndex = 0;
            for (const HeapStringPath& path : heapStringSlotPaths[item->slot]) {
                if (!path.empty() && path.front() == item->field)
                    emitHeapStringPathDrop(base + ".replace" + std::to_string(replacedPathIndex++),
                                           current, item->objectType, path);
            }
            out << "  " << updated << " = insertvalue " << structType << " " << current
                << ", " << llvmType(field.type) << " " << value(item->value) << ", " << item->field << "\n";
            out << "  store " << structType << " " << updated << ", ptr " << slotName(item->slot) << "\n";
            HeapStringPaths slotPaths = removeFieldPaths(heapStringSlotPaths[item->slot], item->field);
            const HeapStringPaths valuePaths = prefixPaths(pathsForValue(item->value), item->field);
            slotPaths.insert(valuePaths.begin(), valuePaths.end());
            if (slotPaths.empty()) {
                heapStringSlotPaths.erase(item->slot);
            } else {
                heapStringSlotPaths[item->slot] = slotPaths;
            }
        } else if (const auto* item = std::get_if<IrStore>(&instruction)) {
            if (!isLlvmValueType(item->type))
                throw std::runtime_error("backend LLVM: type de store non supporté " +
                                         typeName(item->type));
            out << "  store " << llvmType(item->type) << " " << value(item->value)
                << ", ptr " << slotName(item->slot) << "\n";
            if (item->type == ValueType::String) {
                if (heapStringValues.contains(item->value)) {
                    heapStringSlots.insert(item->slot);
                } else {
                    heapStringSlots.erase(item->slot);
                }
            }
            const HeapStringPaths storedPaths = pathsForValue(item->value);
            if (storedPaths.empty()) {
                heapStringSlotPaths.erase(item->slot);
            } else {
                heapStringSlotPaths[item->slot] = storedPaths;
            }
        } else if (const auto* item = std::get_if<IrLoad>(&instruction)) {
            if (!isLlvmValueType(item->type))
                throw std::runtime_error("backend LLVM: type de load non supporté " +
                                         typeName(item->type));
            const std::string output = "%v" + std::to_string(item->output);
            out << "  " << output << " = load " << llvmType(item->type)
                << ", ptr " << slotName(item->slot) << "\n";
            values[item->output] = output;
            if (item->type == ValueType::String && heapStringSlots.contains(item->slot))
                heapStringValues.insert(item->output);
            if (const auto found = heapStringSlotPaths.find(item->slot); found != heapStringSlotPaths.end())
                rememberValuePaths(item->output, found->second);
        } else if (const auto* item = std::get_if<IrStringConcat>(&instruction)) {
            const std::string base = "%v" + std::to_string(item->output);
            const std::string leftPtr = extractStringPart(base + ".left", item->left, 0);
            const std::string leftLen = extractStringPart(base + ".left", item->left, 1);
            const std::string rightPtr = extractStringPart(base + ".right", item->right, 0);
            const std::string rightLen = extractStringPart(base + ".right", item->right, 1);
            const std::string length = base + ".len";
            const std::string allocationSize = base + ".alloc_size";
            const std::string raw = base + ".raw";
            const std::string lenPtr = base + ".len_ptr";
            const std::string data = base + ".data";
            const std::string rightDst = base + ".right_dst";
            const std::string pairPtr = base + ".pair_ptr";
            out << "  " << length << " = add i64 " << leftLen << ", " << rightLen << "\n"
                << "  " << allocationSize << " = add i64 " << length << ", 16\n"
                << "  " << raw << " = call ptr @malloc(i64 " << allocationSize << ")\n"
                << "  store i64 1, ptr " << raw << "\n"
                << "  " << lenPtr << " = getelementptr i8, ptr " << raw << ", i64 8\n"
                << "  store i64 " << length << ", ptr " << lenPtr << "\n"
                << "  " << data << " = getelementptr i8, ptr " << raw << ", i64 16\n"
                << "  call ptr @memcpy(ptr " << data << ", ptr " << leftPtr << ", i64 " << leftLen << ")\n"
                << "  " << rightDst << " = getelementptr i8, ptr " << data << ", i64 " << leftLen << "\n"
                << "  call ptr @memcpy(ptr " << rightDst << ", ptr " << rightPtr << ", i64 " << rightLen << ")\n"
                << "  " << pairPtr << " = insertvalue { ptr, i64 } undef, ptr " << data << ", 0\n"
                << "  " << base << " = insertvalue { ptr, i64 } " << pairPtr << ", i64 " << length << ", 1\n";
            values[item->output] = base;
            heapStringValues.insert(item->output);
            rememberValuePaths(item->output, HeapStringPaths{HeapStringPath{}});
        } else if (const auto* item = std::get_if<IrStringLength>(&instruction)) {
            const ValueType type = program.valueTypes.at(item->string);
            if (type != ValueType::String && type != ValueType::StringView)
                throw std::runtime_error("backend LLVM: longueur hors chaîne non supportée");
            const std::string wide = "%v" + std::to_string(item->output) + ".wide";
            const std::string output = "%v" + std::to_string(item->output);
            out << "  " << wide << " = extractvalue " << llvmType(type) << " "
                << value(item->string) << ", 1\n"
                << "  " << output << " = trunc i64 " << wide << " to i32\n";
            values[item->output] = output;
        } else if (const auto* item = std::get_if<IrStringEmpty>(&instruction)) {
            const ValueType type = program.valueTypes.at(item->string);
            if (type != ValueType::String && type != ValueType::StringView)
                throw std::runtime_error("backend LLVM: isEmpty hors chaîne non supporté");
            const std::string length = "%v" + std::to_string(item->output) + ".length";
            const std::string output = "%v" + std::to_string(item->output);
            out << "  " << length << " = extractvalue " << llvmType(type) << " "
                << value(item->string) << ", 1\n"
                << "  " << output << " = icmp eq i64 " << length << ", 0\n";
            values[item->output] = output;
        } else if (const auto* item = std::get_if<IrStringDecodeAt>(&instruction)) {
            const ValueType type = program.valueTypes.at(item->string);
            if (type != ValueType::String && type != ValueType::StringView)
                throw std::runtime_error("backend LLVM: décodage UTF-8 hors chaîne non supporté");
            const std::string output = "%v" + std::to_string(item->output);
            values[item->output] = emitStringDecodeAtByte(output, item->string, item->offset);
        } else if (const auto* item = std::get_if<IrStringNextOffset>(&instruction)) {
            const ValueType type = program.valueTypes.at(item->string);
            if (type != ValueType::String && type != ValueType::StringView)
                throw std::runtime_error("backend LLVM: offset UTF-8 hors chaîne non supporté");
            const std::string decoded = emitStringDecodeAtByte("%v" + std::to_string(item->output) + ".decoded",
                item->string, item->offset);
            const std::string output = "%v" + std::to_string(item->output);
            const std::string invalid = output + ".invalid";
            const std::string one = output + ".one";
            const std::string two = output + ".two";
            const std::string three = output + ".three";
            const std::string plus1 = output + ".plus1";
            const std::string plus2 = output + ".plus2";
            const std::string plus3 = output + ".plus3";
            const std::string plus4 = output + ".plus4";
            const std::string tail3 = output + ".tail3";
            const std::string tail2 = output + ".tail2";
            const std::string tail1 = output + ".tail1";
            out << "  " << invalid << " = icmp slt i32 " << decoded << ", 0\n"
                << "  " << one << " = icmp ule i32 " << decoded << ", 127\n"
                << "  " << two << " = icmp ule i32 " << decoded << ", 2047\n"
                << "  " << three << " = icmp ule i32 " << decoded << ", 65535\n"
                << "  " << plus1 << " = add i32 " << value(item->offset) << ", 1\n"
                << "  " << plus2 << " = add i32 " << value(item->offset) << ", 2\n"
                << "  " << plus3 << " = add i32 " << value(item->offset) << ", 3\n"
                << "  " << plus4 << " = add i32 " << value(item->offset) << ", 4\n"
                << "  " << tail3 << " = select i1 " << three << ", i32 " << plus3 << ", i32 " << plus4 << "\n"
                << "  " << tail2 << " = select i1 " << two << ", i32 " << plus2 << ", i32 " << tail3 << "\n"
                << "  " << tail1 << " = select i1 " << one << ", i32 " << plus1 << ", i32 " << tail2 << "\n"
                << "  " << output << " = select i1 " << invalid << ", i32 -1, i32 " << tail1 << "\n";
            values[item->output] = output;
        } else if (const auto* item = std::get_if<IrDrop>(&instruction)) {
            if (!isLlvmValueType(item->type) && item->type != ValueType::Unit)
                throw std::runtime_error("backend LLVM: drop non supporté " + typeName(item->type));
            if (item->type == ValueType::String && heapStringValues.contains(item->value)) {
                emitStringFree("%drop" + std::to_string(item->value), value(item->value), item->type);
                heapStringValues.erase(item->value);
                heapStringValuePaths.erase(item->value);
            } else if (item->type.kind == ValueType::Kind::Struct) {
                const HeapStringPaths paths = pathsForValue(item->value);
                std::size_t pathIndex = 0;
                for (const HeapStringPath& path : paths)
                    emitHeapStringPathDrop("%drop" + std::to_string(item->value) + ".path" +
                                               std::to_string(pathIndex++),
                                           value(item->value), item->type, path);
                heapStringValuePaths.erase(item->value);
            }
        } else if (const auto* item = std::get_if<IrRetain>(&instruction)) {
            if (!isLlvmValueType(item->type) && item->type != ValueType::Unit)
                throw std::runtime_error("backend LLVM: retain non supporté " + typeName(item->type));
            if (item->type == ValueType::String && heapStringValues.contains(item->value)) {
                emitStringRetain("%retain" + std::to_string(item->value), value(item->value), item->type);
            } else if (item->type.kind == ValueType::Kind::Struct) {
                const HeapStringPaths paths = pathsForValue(item->value);
                std::size_t pathIndex = 0;
                for (const HeapStringPath& path : paths)
                    emitHeapStringPathRetain("%retain" + std::to_string(item->value) + ".path" +
                                                 std::to_string(pathIndex++),
                                             value(item->value), item->type, path);
            }
        } else {
            unsupported("complexe");
        }
    }
    if (openFunction) {
        if (!terminated) out << "  ret i32 0\n";
        out << "}\n";
    }
    return out.str();
}

std::string FasmCodeGenerator::generate(const IrProgram& program) {
    return generate(IrVerifier::verify(program, IrVerificationMode::Executable));
}

std::string FasmCodeGenerator::generate(const VerifiedIrProgram& verified) {
    if (verified.mode() != IrVerificationMode::Executable)
        throw IrVerificationError("IRV004", "mode objet fourni au codegen exécutable");
    return generateUnchecked(verified.program());
}

std::string FasmCodeGenerator::generateUnchecked(const IrProgram& program) {
    const std::size_t frameSize = align16(slotBytes(program) + valueBytes(program));
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "format ELF64 executable 3\n"
           "entry start\n\n"
           "segment readable executable\n"
           "start:\n"
           "    mov rbp, rsp\n";
    if (frameSize != 0) out << "    sub rsp, " << frameSize << '\n';

    std::size_t resourceSequence = 0;
    for (const IrInstruction& instruction : program.instructions) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, IrUnit>) {
                // Unit is a logical SSA value with no runtime representation.
            } else if constexpr (std::is_same_v<T, IrConst>) {
                out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], " << item.value << '\n';
            } else if constexpr (std::is_same_v<T, IrDoubleConst>) {
                out << "    movsd xmm0, qword [double_const_" << item.output << "]\n"
                    << "    movsd qword [rbp-" << valueOffset(program, item.output) << "], xmm0\n";
            } else if constexpr (std::is_same_v<T, IrStringConst>) {
                const std::size_t offset = valueOffset(program, item.output);
                out << "    lea rax, [string_const_" << item.output << "]\n"
                    << "    mov qword [rbp-" << offset << "], rax\n"
                    << "    mov qword [rbp-" << offset - 8U << "], " << item.utf8.size() << '\n';
            } else if constexpr (std::is_same_v<T, IrStringConcat>) {
                const std::size_t left = valueOffset(program, item.left);
                const std::size_t right = valueOffset(program, item.right);
                const std::size_t output = valueOffset(program, item.output);
                out << "    push r12\n"
                    << "    push r13\n"
                    << "    mov r12, qword [rbp-" << left - 8U << "]\n"
                    << "    add r12, qword [rbp-" << right - 8U << "]\n"
                    << "    jc ir_string_size_error\n"
                    << "    mov rsi, r12\n"
                    << "    add rsi, 16\n"
                    << "    jc ir_string_size_error\n"
                    << "    mov eax, 9\n"
                    << "    xor edi, edi\n"
                    << "    mov edx, 3\n"
                    << "    mov r10d, 34\n"
                    << "    mov r8, -1\n"
                    << "    xor r9d, r9d\n"
                    << "    syscall\n"
                    << "    test rax, rax\n"
                    << "    js ir_string_allocation_error\n"
                    << "    mov qword [rax], 1\n"
                    << "    mov qword [rax+8], r12\n"
                    << "    lea r13, [rax+16]\n"
                    << "    mov qword [rbp-" << output << "], r13\n"
                    << "    mov qword [rbp-" << output - 8U << "], r12\n"
                    << "    mov rdi, r13\n"
                    << "    mov rsi, qword [rbp-" << left << "]\n"
                    << "    mov rcx, qword [rbp-" << left - 8U << "]\n"
                    << "    rep movsb\n"
                    << "    mov rsi, qword [rbp-" << right << "]\n"
                    << "    mov rcx, qword [rbp-" << right - 8U << "]\n"
                    << "    rep movsb\n"
                    << "    pop r13\n"
                    << "    pop r12\n";
            } else if constexpr (std::is_same_v<T, IrStringLength>) {
                out << "    mov rax, qword [rbp-" << valueOffset(program, item.string) - 8U << "]\n"
                    << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrStringEmpty>) {
                out << "    cmp qword [rbp-" << valueOffset(program, item.string) - 8U << "], 0\n"
                    << "    sete al\n"
                    << "    mov byte [rbp-" << valueOffset(program, item.output) << "], al\n";
            } else if constexpr (std::is_same_v<T, IrStringDecodeAt>) {
                const std::size_t stringOffset = valueOffset(program, item.string);
                const std::size_t offsetOffset = valueOffset(program, item.offset);
                const std::size_t outputOffset = valueOffset(program, item.output);
                const std::string two = "ir_string_decode_two_" + std::to_string(item.output);
                const std::string three = "ir_string_decode_three_" + std::to_string(item.output);
                const std::string four = "ir_string_decode_four_" + std::to_string(item.output);
                const std::string done = "ir_string_decode_done_" + std::to_string(item.output);
                out << "    mov rsi, qword [rbp-" << stringOffset << "]\n"
                    << "    movsxd rcx, dword [rbp-" << offsetOffset << "]\n"
                    << "    movzx eax, byte [rsi+rcx]\n"
                    << "    cmp eax, 80h\n"
                    << "    jb " << done << "\n"
                    << "    cmp eax, 0E0h\n"
                    << "    jb " << two << "\n"
                    << "    cmp eax, 0F0h\n"
                    << "    jb " << three << "\n"
                    << "    jmp " << four << "\n"
                    << two << ":\n"
                    << "    and eax, 1Fh\n"
                    << "    shl eax, 6\n"
                    << "    movzx edx, byte [rsi+rcx+1]\n"
                    << "    and edx, 3Fh\n"
                    << "    or eax, edx\n"
                    << "    jmp " << done << "\n"
                    << three << ":\n"
                    << "    and eax, 0Fh\n"
                    << "    shl eax, 12\n"
                    << "    movzx edx, byte [rsi+rcx+1]\n"
                    << "    and edx, 3Fh\n"
                    << "    shl edx, 6\n"
                    << "    or eax, edx\n"
                    << "    movzx edx, byte [rsi+rcx+2]\n"
                    << "    and edx, 3Fh\n"
                    << "    or eax, edx\n"
                    << "    jmp " << done << "\n"
                    << four << ":\n"
                    << "    and eax, 07h\n"
                    << "    shl eax, 18\n"
                    << "    movzx edx, byte [rsi+rcx+1]\n"
                    << "    and edx, 3Fh\n"
                    << "    shl edx, 12\n"
                    << "    or eax, edx\n"
                    << "    movzx edx, byte [rsi+rcx+2]\n"
                    << "    and edx, 3Fh\n"
                    << "    shl edx, 6\n"
                    << "    or eax, edx\n"
                    << "    movzx edx, byte [rsi+rcx+3]\n"
                    << "    and edx, 3Fh\n"
                    << "    or eax, edx\n"
                    << done << ":\n"
                    << "    mov dword [rbp-" << outputOffset << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrStringNextOffset>) {
                const std::size_t stringOffset = valueOffset(program, item.string);
                const std::size_t offsetOffset = valueOffset(program, item.offset);
                const std::size_t outputOffset = valueOffset(program, item.output);
                const std::string oneLabel = "ir_string_next_one_" + std::to_string(item.output);
                const std::string twoLabel = "ir_string_next_two_" + std::to_string(item.output);
                const std::string threeLabel = "ir_string_next_three_" + std::to_string(item.output);
                const std::string done = "ir_string_next_done_" + std::to_string(item.output);
                out << "    mov rsi, qword [rbp-" << stringOffset << "]\n"
                    << "    mov eax, dword [rbp-" << offsetOffset << "]\n"
                    << "    movsxd rcx, eax\n"
                    << "    movzx edx, byte [rsi+rcx]\n"
                    << "    cmp edx, 80h\n"
                    << "    jb " << oneLabel << "\n"
                    << "    cmp edx, 0E0h\n"
                    << "    jb " << twoLabel << "\n"
                    << "    cmp edx, 0F0h\n"
                    << "    jb " << threeLabel << "\n"
                    << "    add eax, 4\n"
                    << "    jmp " << done << "\n"
                    << oneLabel << ":\n"
                    << "    add eax, 1\n"
                    << "    jmp " << done << "\n"
                    << twoLabel << ":\n"
                    << "    add eax, 2\n"
                    << "    jmp " << done << "\n"
                    << threeLabel << ":\n"
                    << "    add eax, 3\n"
                    << done << ":\n"
                    << "    mov dword [rbp-" << outputOffset << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrArrayConstruct>) {
                const std::size_t output = valueOffset(program, item.output);
                const std::size_t elementBytes = valueTypeSize(*item.type.element);
                for (std::size_t i = 0; i < item.elements.size(); ++i) {
                    const std::string source = "[rbp-" +
                        std::to_string(valueOffset(program, item.elements[i])) + "]";
                    const std::string target = "[rbp-" + std::to_string(output) +
                        (i == 0 ? "]" : "+" + std::to_string(i * elementBytes) + "]");
                    emitBlockCopy(out, source, target, elementBytes);
                }
            } else if constexpr (std::is_same_v<T, IrVecConstruct>) {
                const std::size_t output = valueOffset(program, item.output);
                out << "    mov qword [rbp-" << output << "], 0\n"
                    << "    mov qword [rbp-" << output - 8U << "], 0\n"
                    << "    mov qword [rbp-" << output - 16U << "], 0\n";
            } else if constexpr (std::is_same_v<T, IrVecProperty>) {
                const std::string address = vecMutationAddress(program, item.target);
                const std::size_t output = valueOffset(program, item.output);
                emitVecMutationTargetSetup(out, program, item.target);
                if (item.property == "length")
                    out << "    mov eax, dword " << displacedAddress(address, 8U) << "\n";
                else if (item.property == "capacity")
                    out << "    mov eax, dword " << displacedAddress(address, 16U) << "\n";
                else
                    out << "    cmp qword " << displacedAddress(address, 8U) << ", 0\n"
                        << "    sete al\n"
                        << "    movzx eax, al\n";
                out << "    mov dword [rbp-" << output << "], eax\n";
                emitVecMutationTargetCleanup(out, item.target);
            } else if constexpr (std::is_same_v<T, IrVecReserve>) {
                const std::size_t id = resourceSequence++;
                const std::string address = vecMutationAddress(program, item.target);
                const std::size_t elementSize = valueTypeSize(*item.type.element);
                const std::string capacityReady = "ir_vec_capacity_ready_" + std::to_string(id);
                const std::string requestedReady = "ir_vec_requested_ready_" + std::to_string(id);
                const std::string oldReleased = "ir_vec_old_released_" + std::to_string(id);
                const std::string done = "ir_vec_reserve_done_" + std::to_string(id);
                emitVecMutationTargetSetup(out, program, item.target);
                out << "    push r12\n"
                    << "    push r13\n"
                    << "    push r14\n"
                    << "    push r15\n"
                    << "    movsxd r12, dword [rbp-" << valueOffset(program, item.additional) << "]\n"
                    << "    test r12, r12\n"
                    << "    js ir_vec_allocation_error\n"
                    << "    mov r13, qword " << displacedAddress(address, 8U) << "\n"
                    << "    add r13, r12\n"
                    << "    jc ir_vec_allocation_error\n"
                    << "    cmp r13, 2147483647\n"
                    << "    ja ir_vec_allocation_error\n"
                    << "    mov r14, qword " << displacedAddress(address, 16U) << "\n"
                    << "    cmp r13, r14\n"
                    << "    jbe " << done << "\n"
                    << "    mov r15, r14\n"
                    << "    shl r15, 1\n"
                    << "    jc ir_vec_allocation_error\n"
                    << "    cmp r15, 4\n"
                    << "    jae " << capacityReady << "\n"
                    << "    mov r15, 4\n"
                    << capacityReady << ":\n"
                    << "    cmp r15, r13\n"
                    << "    jae " << requestedReady << "\n"
                    << "    mov r15, r13\n"
                    << requestedReady << ":\n"
                    << "    cmp r15, 2147483647\n"
                    << "    ja ir_vec_allocation_error\n"
                    << "    mov rsi, r15\n"
                    << "    imul rsi, " << elementSize << "\n"
                    << "    jo ir_vec_allocation_error\n"
                    << "    mov eax, 9\n"
                    << "    xor edi, edi\n"
                    << "    mov edx, 3\n"
                    << "    mov r10d, 34\n"
                    << "    mov r8, -1\n"
                    << "    xor r9d, r9d\n"
                    << "    syscall\n"
                    << "    test rax, rax\n"
                    << "    js ir_vec_allocation_error\n"
                    << "    mov r12, rax\n"
                    << "    mov rdi, r12\n"
                    << "    mov rsi, qword " << address << "\n"
                    << "    mov rcx, qword " << displacedAddress(address, 8U) << "\n"
                    << "    imul rcx, " << elementSize << "\n"
                    << "    rep movsb\n"
                    << "    mov rdi, qword " << address << "\n"
                    << "    test rdi, rdi\n"
                    << "    jz " << oldReleased << "\n"
                    << "    mov rsi, r14\n"
                    << "    imul rsi, " << elementSize << "\n"
                    << "    mov eax, 11\n"
                    << "    syscall\n"
                    << oldReleased << ":\n"
                    << "    mov qword " << address << ", r12\n"
                    << "    mov qword " << displacedAddress(address, 16U) << ", r15\n"
                    << done << ":\n"
                    << "    pop r15\n"
                    << "    pop r14\n"
                    << "    pop r13\n"
                    << "    pop r12\n";
                emitVecMutationTargetCleanup(out, item.target);
            } else if constexpr (std::is_same_v<T, IrVecPush>) {
                const std::string address = vecMutationAddress(program, item.target);
                const std::size_t elementSize = valueTypeSize(*item.type.element);
                emitVecMutationTargetSetup(out, program, item.target);
                out << "    mov rdi, qword " << address << "\n"
                    << "    mov rax, qword " << displacedAddress(address, 8U) << "\n"
                    << "    imul rax, " << elementSize << "\n"
                    << "    add rdi, rax\n";
                emitBlockCopy(out,
                    "[rbp-" + std::to_string(valueOffset(program, item.value)) + "]",
                    "[rdi]", elementSize);
                out << "    inc qword " << displacedAddress(address, 8U) << "\n";
                emitVecMutationTargetCleanup(out, item.target);
            } else if constexpr (std::is_same_v<T, IrVecClear>) {
                const std::size_t id = resourceSequence++;
                const std::string address = vecMutationAddress(program, item.target);
                const std::string loop = "ir_vec_clear_loop_" + std::to_string(id);
                const std::string done = "ir_vec_clear_done_" + std::to_string(id);
                emitVecMutationTargetSetup(out, program, item.target);
                if (valueTypeNeedsDrop(*item.type.element)) {
                    out << "    push r12\n"
                        << "    push r13\n"
                        << "    push r14\n"
                        << "    mov r12, qword " << address << "\n"
                        << "    mov r13, qword " << displacedAddress(address, 8U) << "\n"
                        << loop << ":\n"
                        << "    test r13, r13\n"
                        << "    jz " << done << "\n"
                        << "    dec r13\n"
                        << "    mov r14, r13\n"
                        << "    imul r14, " << valueTypeSize(*item.type.element) << "\n"
                        << "    add r14, r12\n";
                    emitValueDrop(out, "[r14]", *item.type.element,
                                  "ir_vec_clear_element_" + std::to_string(id));
                    out << "    jmp " << loop << "\n"
                        << done << ":\n"
                        << "    pop r14\n"
                        << "    pop r13\n"
                        << "    pop r12\n";
                }
                out << "    mov qword " << displacedAddress(address, 8U) << ", 0\n";
                emitVecMutationTargetCleanup(out, item.target);
            } else if constexpr (std::is_same_v<T, IrVecView>) {
                const std::string address = vecMutationAddress(program, item.target);
                const std::size_t output = valueOffset(program, item.output);
                emitVecMutationTargetSetup(out, program, item.target);
                out << "    mov rax, qword " << address << "\n"
                    << "    mov qword [rbp-" << output << "], rax\n"
                    << "    mov rax, qword " << displacedAddress(address, 8U) << "\n"
                    << "    mov qword [rbp-" << output - 8U << "], rax\n";
                emitVecMutationTargetCleanup(out, item.target);
            } else if constexpr (std::is_same_v<T, IrVecGet>) {
                const std::size_t id = resourceSequence++;
                const std::string address = vecMutationAddress(program, item.target);
                const std::size_t output = valueOffset(program, item.output);
                const auto& variants = item.optionType.enumeration->variants;
                const std::size_t some = static_cast<std::size_t>(std::find_if(
                    variants.begin(), variants.end(), [](const EnumVariant& variant) {
                        return variant.name == "Some";
                    }) - variants.begin());
                const std::size_t none = static_cast<std::size_t>(std::find_if(
                    variants.begin(), variants.end(), [](const EnumVariant& variant) {
                        return variant.name == "None";
                    }) - variants.begin());
                const std::string absent = "ir_vec_get_none_" + std::to_string(id);
                const std::string done = "ir_vec_get_done_" + std::to_string(id);
                emitVecMutationTargetSetup(out, program, item.target);
                out << "    movsxd rax, dword [rbp-" << valueOffset(program, item.index) << "]\n"
                    << "    test rax, rax\n"
                    << "    js " << absent << "\n"
                    << "    cmp rax, qword " << displacedAddress(address, 8U) << "\n"
                    << "    jae " << absent << "\n"
                    << "    imul rax, " << valueTypeSize(item.elementType) << "\n"
                    << "    mov rsi, qword " << address << "\n"
                    << "    add rsi, rax\n"
                    << "    mov dword [rbp-" << output << "], " << some << "\n";
                const std::string payload = "[rbp-" + std::to_string(output) + "+" +
                    std::to_string(item.optionType.enumeration->payloadOffset) + "]";
                emitBlockCopy(out, "[rsi]", payload, valueTypeSize(item.elementType));
                if (valueTypeNeedsDrop(item.elementType))
                    emitValueRetain(out, payload, item.elementType,
                                    "ir_vec_get_retain_" + std::to_string(id));
                out << "    jmp " << done << "\n"
                    << absent << ":\n"
                    << "    mov dword [rbp-" << output << "], " << none << "\n"
                    << done << ":\n";
                emitVecMutationTargetCleanup(out, item.target);
            } else if constexpr (std::is_same_v<T, IrVecPop>) {
                const std::size_t id = resourceSequence++;
                const std::string address = vecMutationAddress(program, item.target);
                const std::size_t output = valueOffset(program, item.output);
                const auto& variants = item.optionType.enumeration->variants;
                const std::size_t some = static_cast<std::size_t>(std::find_if(
                    variants.begin(), variants.end(), [](const EnumVariant& variant) {
                        return variant.name == "Some";
                    }) - variants.begin());
                const std::size_t none = static_cast<std::size_t>(std::find_if(
                    variants.begin(), variants.end(), [](const EnumVariant& variant) {
                        return variant.name == "None";
                    }) - variants.begin());
                const std::string absent = "ir_vec_pop_none_" + std::to_string(id);
                const std::string done = "ir_vec_pop_done_" + std::to_string(id);
                emitVecMutationTargetSetup(out, program, item.target);
                out << "    mov rax, qword " << displacedAddress(address, 8U) << "\n"
                    << "    test rax, rax\n"
                    << "    jz " << absent << "\n"
                    << "    dec rax\n"
                    << "    mov qword " << displacedAddress(address, 8U) << ", rax\n"
                    << "    imul rax, " << valueTypeSize(item.elementType) << "\n"
                    << "    mov rsi, qword " << address << "\n"
                    << "    add rsi, rax\n"
                    << "    mov dword [rbp-" << output << "], " << some << "\n";
                emitBlockCopy(out, "[rsi]",
                    "[rbp-" + std::to_string(output) + "+" +
                        std::to_string(item.optionType.enumeration->payloadOffset) + "]",
                    valueTypeSize(item.elementType));
                out << "    jmp " << done << "\n"
                    << absent << ":\n"
                    << "    mov dword [rbp-" << output << "], " << none << "\n"
                    << done << ":\n";
                emitVecMutationTargetCleanup(out, item.target);
            } else if constexpr (std::is_same_v<T, IrVecPopValue>) {
                const std::string address = vecMutationAddress(program, item.target);
                const std::size_t output = valueOffset(program, item.output);
                const std::size_t elementSize = valueTypeSize(item.elementType);
                const std::size_t id = resourceSequence++;
                const std::string done = "ir_vec_pop_value_done_" + std::to_string(id);
                emitVecMutationTargetSetup(out, program, item.target);
                out << "    mov rax, qword " << displacedAddress(address, 8U) << "\n"
                    << "    test rax, rax\n"
                    << "    jz ir_array_bounds_error\n"
                    << "    mov rsi, qword " << address << "\n";
                emitBlockCopy(out, "[rsi]",
                    "[rbp-" + std::to_string(output) + "]",
                    elementSize);
                out << "    mov rdx, qword " << displacedAddress(address, 8U) << "\n"
                    << "    dec rdx\n"
                    << "    mov qword " << displacedAddress(address, 8U) << ", rdx\n"
                    << "    test rdx, rdx\n"
                    << "    jz " << done << "\n"
                    << "    mov rdi, qword " << address << "\n"
                    << "    mov rsi, rdi\n"
                    << "    add rsi, " << elementSize << "\n"
                    << "    mov rcx, rdx\n"
                    << "    imul rcx, " << elementSize << "\n"
                    << "    cld\n"
                    << "    rep movsb\n"
                    << done << ":\n";
                emitVecMutationTargetCleanup(out, item.target);
            } else if constexpr (std::is_same_v<T, IrVecSet>) {
                const std::size_t id = resourceSequence++;
                const std::string address = vecMutationAddress(program, item.target);
                emitVecMutationTargetSetup(out, program, item.target);
                out << "    push r12\n"
                    << "    movsxd rax, dword [rbp-" << valueOffset(program, item.index) << "]\n"
                    << "    test rax, rax\n"
                    << "    js ir_array_bounds_error\n"
                    << "    cmp rax, qword " << displacedAddress(address, 8U) << "\n"
                    << "    jae ir_array_bounds_error\n"
                    << "    imul rax, " << valueTypeSize(item.elementType) << "\n"
                    << "    mov r12, qword " << address << "\n"
                    << "    add r12, rax\n";
                if (valueTypeNeedsDrop(item.elementType))
                    emitValueDrop(out, "[r12]", item.elementType,
                                  "ir_vec_set_old_" + std::to_string(id));
                emitBlockCopy(out,
                    "[rbp-" + std::to_string(valueOffset(program, item.value)) + "]",
                    "[r12]", valueTypeSize(item.elementType));
                out << "    pop r12\n";
                emitVecMutationTargetCleanup(out, item.target);
            } else if constexpr (std::is_same_v<T, IrStructConstruct>) {
                const std::size_t output = valueOffset(program, item.output);
                for (std::size_t i = 0; i < item.fields.size(); ++i) {
                    const StructField& field = item.type.structure->fields[i];
                    emitBlockCopy(out, "[rbp-" + std::to_string(valueOffset(program, item.fields[i])) + "]",
                        "[rbp-" + std::to_string(output) + "+" + std::to_string(field.offset) + "]",
                        valueTypeSize(field.type));
                }
            } else if constexpr (std::is_same_v<T, IrEnumConstruct>) {
                const std::size_t output = valueOffset(program, item.output);
                out << "    mov dword [rbp-" << output << "], " << item.variant << '\n';
                const EnumVariant& variant = item.type.enumeration->variants[item.variant];
                for (std::size_t i = 0; i < item.fields.size(); ++i) {
                    const StructField& field = variant.fields[i];
                    emitBlockCopy(out,
                        "[rbp-" + std::to_string(valueOffset(program, item.fields[i])) + "]",
                        "[rbp-" + std::to_string(output) + "+" +
                            std::to_string(item.type.enumeration->payloadOffset + field.offset) + "]",
                        valueTypeSize(field.type));
                }
            } else if constexpr (std::is_same_v<T, IrEnumTag>) {
                out << "    mov eax, dword [rbp-" << valueOffset(program, item.input) << "]\n"
                    << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrEnumFieldLoad>) {
                const StructField& field =
                    item.type.enumeration->variants[item.variant].fields[item.field];
                emitBlockCopy(out,
                    "[rbp-" + std::to_string(valueOffset(program, item.input)) + "+" +
                        std::to_string(item.type.enumeration->payloadOffset + field.offset) + "]",
                    "[rbp-" + std::to_string(valueOffset(program, item.output)) + "]",
                    valueTypeSize(program.valueTypes[item.output]));
            } else if constexpr (std::is_same_v<T, IrFieldLoad>) {
                const StructField& field = item.objectType.structure->fields[item.field];
                emitBlockCopy(out, "[rbp-" + std::to_string(valueOffset(program, item.object)) + "+" + std::to_string(field.offset) + "]",
                    "[rbp-" + std::to_string(valueOffset(program, item.output)) + "]", valueTypeSize(field.type));
            } else if constexpr (std::is_same_v<T, IrFieldStore>) {
                const StructField& field = item.objectType.structure->fields[item.field];
                emitBlockCopy(out, "[rbp-" + std::to_string(valueOffset(program, item.value)) + "]",
                    "[rbp-" + std::to_string(slotOffset(program, item.slot)) + "+" +
                        std::to_string(field.offset) + "]", valueTypeSize(field.type));
            } else if constexpr (std::is_same_v<T, IrSliceConstruct>) {
                const std::size_t output = valueOffset(program, item.output);
                out << "    mov rax, qword [rbp-" << valueOffset(program, item.reference) << "]\n"
                    << "    mov qword [rbp-" << output << "], rax\n"
                    << "    mov qword [rbp-" << output - 8U << "], " << item.length << "\n";
            } else if constexpr (std::is_same_v<T, IrSliceLength>) {
                out << "    mov rax, qword [rbp-" << valueOffset(program, item.slice) - 8U << "]\n"
                    << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrBoxConstruct>) {
                out << "    mov eax, 9\n"
                    << "    xor edi, edi\n"
                    << "    mov esi, " << valueTypeSize(item.elementType) << "\n"
                    << "    mov edx, 3\n"
                    << "    mov r10d, 34\n"
                    << "    mov r8, -1\n"
                    << "    xor r9d, r9d\n"
                    << "    syscall\n"
                    << "    test rax, rax\n"
                    << "    js ir_box_allocation_error\n"
                    << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rax\n"
                    << "    mov rdi, rax\n";
                emitBlockCopy(out,
                    "[rbp-" + std::to_string(valueOffset(program, item.value)) + "]",
                    "[rdi]", valueTypeSize(item.elementType));
            } else if constexpr (std::is_same_v<T, IrIndexLoad>) {
                const std::size_t elementBytes = valueTypeSize(*item.arrayType.element);
                out << "    movsxd rax, dword [rbp-" << valueOffset(program, item.index) << "]\n"
                    << "    test rax, rax\n"
                    << "    js ir_array_bounds_error\n";
                if (item.arrayIsSlice)
                    out << "    cmp rax, qword [rbp-" << valueOffset(program, item.array) - 8U << "]\n";
                else
                    out << "    cmp rax, " << item.arrayType.length << "\n";
                out << "    jae ir_array_bounds_error\n"
                    << "    imul rax, " << elementBytes << "\n";
                if (item.arrayIsReference || item.arrayIsSlice)
                    out << "    mov rsi, qword [rbp-" << valueOffset(program, item.array) << "]\n";
                else
                    out << "    lea rsi, [rbp-" << valueOffset(program, item.array) << "]\n";
                out << "    add rsi, rax\n";
                emitBlockCopy(out, "[rsi]",
                    "[rbp-" + std::to_string(valueOffset(program, item.output)) + "]",
                    elementBytes);
            } else if constexpr (std::is_same_v<T, IrIndexAddress>) {
                const std::size_t elementBytes = valueTypeSize(*item.arrayType.element);
                out << "    movsxd rax, dword [rbp-" << valueOffset(program, item.index) << "]\n"
                    << "    test rax, rax\n"
                    << "    js ir_array_bounds_error\n";
                if (item.arrayIsSlice)
                    out << "    cmp rax, qword [rbp-" << valueOffset(program, item.array) - 8U << "]\n";
                else
                    out << "    cmp rax, " << item.arrayType.length << "\n";
                out << "    jae ir_array_bounds_error\n"
                    << "    imul rax, " << elementBytes << "\n";
                if (item.arrayIsReference || item.arrayIsSlice)
                    out << "    mov rsi, qword [rbp-" << valueOffset(program, item.array) << "]\n";
                else
                    out << "    lea rsi, [rbp-" << valueOffset(program, item.array) << "]\n";
                out << "    add rsi, rax\n"
                    << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rsi\n";
            } else if constexpr (std::is_same_v<T, IrIndexStore>) {
                if (item.arrayIsReference || item.arrayIsSlice) {
                    out << "    mov rdi, qword [rbp-" << valueOffset(program, item.array) << "]\n";
                } else {
                    const IrSlot& slot = program.slots[item.slot];
                    if (slot.global)
                        out << "    lea rdi, [" << globalLabel(program.slots[item.slot]) << "]\n";
                    else
                        out << "    lea rdi, [rbp-" << slotOffset(program, item.slot) << "]\n";
                }
                ValueType indexedType = item.arrayType;
                bool firstIndex = true;
                for (ValueId index : item.indexes) {
                    const std::size_t elementBytes = valueTypeSize(*indexedType.element);
                    out << "    movsxd rax, dword [rbp-" << valueOffset(program, index) << "]\n"
                        << "    test rax, rax\n"
                        << "    js ir_array_bounds_error\n";
                    if (item.arrayIsSlice && firstIndex)
                        out << "    cmp rax, qword [rbp-" << valueOffset(program, item.array) - 8U << "]\n";
                    else
                        out << "    cmp rax, " << indexedType.length << "\n";
                    out << "    jae ir_array_bounds_error\n"
                        << "    imul rax, " << elementBytes << "\n"
                        << "    add rdi, rax\n";
                    indexedType = *indexedType.element;
                    firstIndex = false;
                }
                emitBlockCopy(out,
                    "[rbp-" + std::to_string(valueOffset(program, item.value)) + "]",
                    "[rdi]", valueTypeSize(indexedType));
            } else if constexpr (std::is_same_v<T, IrAddressOf>) {
                const IrSlot& slot = program.slots[item.slot];
                if (slot.global)
                    out << "    lea rax, [" << globalLabel(program.slots[item.slot]) << "]\n";
                else
                    out << "    lea rax, [rbp-" << slotOffset(program, item.slot) << "]\n";
                out << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rax\n";
            } else if constexpr (std::is_same_v<T, IrDereference>) {
                out << "    mov rsi, qword [rbp-" << valueOffset(program, item.reference) << "]\n";
                emitBlockCopy(out, "[rsi]",
                    "[rbp-" + std::to_string(valueOffset(program, item.output)) + "]",
                    valueTypeSize(item.type));
            } else if constexpr (std::is_same_v<T, IrDereferenceStore>) {
                out << "    mov rdi, qword [rbp-" << valueOffset(program, item.reference) << "]\n";
                emitBlockCopy(out,
                    "[rbp-" + std::to_string(valueOffset(program, item.value)) + "]",
                    "[rdi]", valueTypeSize(item.type));
            } else if constexpr (std::is_same_v<T, IrLoad>) {
                const IrSlot& slot = program.slots[item.slot];
                const std::string address = slot.global
                    ? "[" + globalLabel(slot) + "]"
                    : "[rbp-" + std::to_string(slotOffset(program, item.slot)) + "]";
                if (item.type == ValueType::Unit) {
                    // Unit slots contain no payload.
                } else if (item.type.kind == ValueType::Kind::Array ||
                    isAggregateValue(item.type) ||
                    isPairValue(item.type)) {
                    emitBlockCopy(out, address,
                        "[rbp-" + std::to_string(valueOffset(program, item.output)) + "]",
                        valueTypeSize(item.type));
                } else if (item.type.kind == ValueType::Kind::Reference ||
                           item.type.kind == ValueType::Kind::Box) {
                    out << "    mov rax, qword " << address << "\n"
                        << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rax\n";
                } else if (item.type == ValueType::String) {
                    const std::size_t offset = valueOffset(program, item.output);
                    out << "    mov rax, qword " << address << "\n"
                        << "    mov qword [rbp-" << offset << "], rax\n"
                        << "    mov rax, qword " << address.substr(0, address.size() - 1) << "+8]\n"
                        << "    mov qword [rbp-" << offset - 8U << "], rax\n";
                } else if (item.type == ValueType::Double) {
                    out << "    movsd xmm0, qword " << address << "\n"
                        << "    movsd qword [rbp-" << valueOffset(program, item.output) << "], xmm0\n";
                } else if (item.type == ValueType::Byte || item.type == ValueType::Bool) {
                    out << "    movzx eax, byte " << address << "\n";
                    out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                } else {
                    out << "    mov eax, dword " << address << "\n";
                    out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                }
            } else if constexpr (std::is_same_v<T, IrConvert>) {
                if (item.target == ValueType::String && item.source == ValueType::Bool) {
                    const std::size_t output = valueOffset(program, item.output);
                    out << "    cmp dword [rbp-" << valueOffset(program, item.input) << "], 0\n"
                        << "    je ir_string_bool_false_" << item.output << "\n"
                        << "    lea rax, [string_bool_true]\n"
                        << "    mov edx, 4\n"
                        << "    jmp ir_string_bool_done_" << item.output << "\n"
                        << "ir_string_bool_false_" << item.output << ":\n"
                        << "    lea rax, [string_bool_false]\n"
                        << "    mov edx, 5\n"
                        << "ir_string_bool_done_" << item.output << ":\n"
                        << "    mov qword [rbp-" << output << "], rax\n"
                        << "    mov qword [rbp-" << output - 8U << "], rdx\n";
                } else if (item.target == ValueType::String && item.source == ValueType::Char) {
                    const std::size_t output = valueOffset(program, item.output);
                    out << "    push r12\n"
                        << "    mov r12d, dword [rbp-" << valueOffset(program, item.input) << "]\n"
                        << "    mov eax, 9\n    xor edi, edi\n    mov esi, 20\n"
                        << "    mov edx, 3\n    mov r10d, 34\n    mov r8, -1\n    xor r9d, r9d\n    syscall\n"
                        << "    test rax, rax\n    js ir_string_allocation_error\n"
                        << "    mov qword [rax], 1\n    mov qword [rax+8], 4\n    lea rdi, [rax+16]\n"
                        << "    cmp r12d, 7Fh\n    ja ir_string_char_two_" << item.output << "\n"
                        << "    mov byte [rdi], r12b\n    mov edx, 1\n    jmp ir_string_char_done_" << item.output << "\n"
                        << "ir_string_char_two_" << item.output << ":\n"
                        << "    cmp r12d, 7FFh\n    ja ir_string_char_three_" << item.output << "\n"
                        << "    mov eax, r12d\n    shr eax, 6\n    or al, 0C0h\n    mov byte [rdi], al\n"
                        << "    mov eax, r12d\n    and al, 3Fh\n    or al, 80h\n    mov byte [rdi+1], al\n"
                        << "    mov edx, 2\n    jmp ir_string_char_done_" << item.output << "\n"
                        << "ir_string_char_three_" << item.output << ":\n"
                        << "    cmp r12d, 0FFFFh\n    ja ir_string_char_four_" << item.output << "\n"
                        << "    mov eax, r12d\n    shr eax, 12\n    or al, 0E0h\n    mov byte [rdi], al\n"
                        << "    mov eax, r12d\n    shr eax, 6\n    and al, 3Fh\n    or al, 80h\n    mov byte [rdi+1], al\n"
                        << "    mov eax, r12d\n    and al, 3Fh\n    or al, 80h\n    mov byte [rdi+2], al\n"
                        << "    mov edx, 3\n    jmp ir_string_char_done_" << item.output << "\n"
                        << "ir_string_char_four_" << item.output << ":\n"
                        << "    mov eax, r12d\n    shr eax, 18\n    or al, 0F0h\n    mov byte [rdi], al\n"
                        << "    mov eax, r12d\n    shr eax, 12\n    and al, 3Fh\n    or al, 80h\n    mov byte [rdi+1], al\n"
                        << "    mov eax, r12d\n    shr eax, 6\n    and al, 3Fh\n    or al, 80h\n    mov byte [rdi+2], al\n"
                        << "    mov eax, r12d\n    and al, 3Fh\n    or al, 80h\n    mov byte [rdi+3], al\n    mov edx, 4\n"
                        << "ir_string_char_done_" << item.output << ":\n"
                        << "    mov qword [rbp-" << output << "], rdi\n"
                        << "    mov qword [rbp-" << output - 8U << "], rdx\n    pop r12\n";
                } else if (item.target == ValueType::String && item.source == ValueType::Double) {
                    const std::size_t output = valueOffset(program, item.output);
                    const std::string id = std::to_string(item.output);
                    out << "    push r12\n    push r13\n    push r14\n    push r15\n"
                        << "    sub rsp, 64\n"
                        << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.input) << "]\n"
                        << "    movq rax, xmm0\n    xor r15d, r15d\n"
                        << "    bt rax, 63\n    jnc ir_string_double_abs_" << id << "\n"
                        << "    mov byte [rsp], '-'\n    inc r15d\n    btr rax, 63\n    movq xmm0, rax\n"
                        << "ir_string_double_abs_" << id << ":\n"
                        << "    mov rdx, 7FF0000000000000h\n    mov rcx, rax\n    and rcx, rdx\n    cmp rcx, rdx\n"
                        << "    jne ir_string_double_finite_" << id << "\n"
                        << "    mov rdx, 000FFFFFFFFFFFFFh\n    test rax, rdx\n    jnz ir_string_double_nan_" << id << "\n"
                        << "    mov byte [rsp+r15], 'i'\n    mov byte [rsp+r15+1], 'n'\n    mov byte [rsp+r15+2], 'f'\n"
                        << "    add r15d, 3\n    jmp ir_string_double_allocate_" << id << "\n"
                        << "ir_string_double_nan_" << id << ":\n    xor r15d, r15d\n"
                        << "    mov byte [rsp], 'n'\n    mov byte [rsp+1], 'a'\n    mov byte [rsp+2], 'n'\n    mov r15d, 3\n"
                        << "    jmp ir_string_double_allocate_" << id << "\n"
                        << "ir_string_double_finite_" << id << ":\n"
                        << "    cvttsd2si r12, xmm0\n"
                        << "    cvtsi2sd xmm1, r12\n    subsd xmm0, xmm1\n"
                        << "    mulsd xmm0, qword [string_double_million]\n"
                        << "    addsd xmm0, qword [string_double_half]\n    cvttsd2si r13, xmm0\n"
                        << "    cmp r13, 1000000\n    jb ir_string_double_integer_" << id << "\n"
                        << "    xor r13d, r13d\n    inc r12\n"
                        << "ir_string_double_integer_" << id << ":\n"
                        << "    lea r14, [rsp+32]\n    xor ecx, ecx\n    mov rax, r12\n    mov r11, 10\n"
                        << "ir_string_double_integer_loop_" << id << ":\n"
                        << "    xor edx, edx\n    div r11\n    add dl, '0'\n    dec r14\n    mov byte [r14], dl\n    inc ecx\n"
                        << "    test rax, rax\n    jnz ir_string_double_integer_loop_" << id << "\n"
                        << "    mov rdi, rsp\n    add rdi, r15\n    mov rsi, r14\n    rep movsb\n"
                        << "    mov r15, rdi\n    sub r15, rsp\n    test r13, r13\n    jz ir_string_double_allocate_" << id << "\n"
                        << "    mov byte [rsp+r15], '.'\n    inc r15d\n"
                        << "    lea r14, [rsp+48]\n    mov ecx, 6\n    mov rax, r13\n"
                        << "ir_string_double_fraction_loop_" << id << ":\n"
                        << "    xor edx, edx\n    div r11\n    add dl, '0'\n    dec r14\n    mov byte [r14], dl\n    loop ir_string_double_fraction_loop_" << id << "\n"
                        << "    mov ecx, 6\n"
                        << "ir_string_double_trim_" << id << ":\n"
                        << "    cmp byte [r14+rcx-1], '0'\n    jne ir_string_double_copy_fraction_" << id << "\n"
                        << "    dec ecx\n    jnz ir_string_double_trim_" << id << "\n"
                        << "ir_string_double_copy_fraction_" << id << ":\n"
                        << "    mov rdi, rsp\n    add rdi, r15\n    mov rsi, r14\n    add r15d, ecx\n    rep movsb\n"
                        << "ir_string_double_allocate_" << id << ":\n"
                        << "    mov eax, 9\n    xor edi, edi\n    mov esi, 80\n    mov edx, 3\n    mov r10d, 34\n"
                        << "    mov r8, -1\n    xor r9d, r9d\n    syscall\n    test rax, rax\n    js ir_string_allocation_error\n"
                        << "    mov qword [rax], 1\n    mov qword [rax+8], 64\n    lea r12, [rax+16]\n"
                        << "    mov rdi, r12\n    mov rsi, rsp\n    mov rcx, r15\n    rep movsb\n"
                        << "    mov qword [rbp-" << output << "], r12\n"
                        << "    mov qword [rbp-" << output - 8U << "], r15\n"
                        << "    add rsp, 64\n    pop r15\n    pop r14\n    pop r13\n    pop r12\n";
                } else if (item.target == ValueType::String &&
                           (item.source == ValueType::Int || item.source == ValueType::Byte)) {
                    const std::size_t output = valueOffset(program, item.output);
                    out << "    push r12\n    push r13\n    push r14\n    push r15\n"
                        << (item.source == ValueType::Byte ? "    movzx rax, byte [rbp-" : "    movsxd rax, dword [rbp-")
                        << valueOffset(program, item.input) << "]\n"
                        << "    sub rsp, 16\n"
                        << "    lea r14, [rsp+16]\n"
                        << "    xor r15d, r15d\n"
                        << "    xor r13d, r13d\n"
                        << "    test rax, rax\n"
                        << "    jns ir_string_int_digits_" << item.output << "\n"
                        << "    neg rax\n    mov r13d, 1\n"
                        << "ir_string_int_digits_" << item.output << ":\n"
                        << "    mov ecx, 10\n"
                        << "ir_string_int_loop_" << item.output << ":\n"
                        << "    xor edx, edx\n    div rcx\n    add dl, '0'\n"
                        << "    dec r14\n    mov byte [r14], dl\n    inc r15\n"
                        << "    test rax, rax\n    jnz ir_string_int_loop_" << item.output << "\n"
                        << "    test r13d, r13d\n    jz ir_string_int_allocate_" << item.output << "\n"
                        << "    dec r14\n    mov byte [r14], '-'\n    inc r15\n"
                        << "ir_string_int_allocate_" << item.output << ":\n"
                        << "    mov eax, 9\n    xor edi, edi\n    mov esi, 32\n"
                        << "    mov edx, 3\n    mov r10d, 34\n    mov r8, -1\n    xor r9d, r9d\n    syscall\n"
                        << "    test rax, rax\n    js ir_string_allocation_error\n"
                        << "    mov qword [rax], 1\n    mov qword [rax+8], 16\n    lea r12, [rax+16]\n"
                        << "    mov rdi, r12\n    mov rsi, r14\n    mov rcx, r15\n    rep movsb\n"
                        << "    mov qword [rbp-" << output << "], r12\n"
                        << "    mov qword [rbp-" << output - 8U << "], r15\n"
                        << "    add rsp, 16\n    pop r15\n    pop r14\n    pop r13\n    pop r12\n";
                } else if (item.source == item.target) {
                    if (item.target == ValueType::String) {
                        const std::size_t input = valueOffset(program, item.input);
                        const std::size_t output = valueOffset(program, item.output);
                        out << "    mov rax, qword [rbp-" << input << "]\n"
                            << "    mov qword [rbp-" << output << "], rax\n"
                            << "    mov rax, qword [rbp-" << input - 8U << "]\n"
                            << "    mov qword [rbp-" << output - 8U << "], rax\n";
                    } else if (item.target == ValueType::Double) {
                        out << "    mov rax, qword [rbp-" << valueOffset(program, item.input) << "]\n"
                            << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rax\n";
                    } else {
                        out << "    mov eax, dword [rbp-" << valueOffset(program, item.input) << "]\n"
                            << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                    }
                } else if (item.target == ValueType::Double) {
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.input) << "]\n"
                        << "    cvtsi2sd xmm0, eax\n"
                        << "    movsd qword [rbp-" << valueOffset(program, item.output) << "], xmm0\n";
                } else if (item.source == ValueType::Double) {
                    out << "    cvttsd2si eax, qword [rbp-" << valueOffset(program, item.input) << "]\n";
                    if (item.target == ValueType::Byte) out << "    and eax, 255\n";
                    out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                } else {
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.input) << "]\n";
                    if (item.target == ValueType::Byte) out << "    and eax, 255\n";
                    if (item.target == ValueType::Char) {
                        out << "    cmp eax, 10FFFFh\n"
                            << "    ja ir_invalid_char_" << item.output << "\n"
                            << "    cmp eax, 0D800h\n"
                            << "    jb ir_valid_char_" << item.output << "\n"
                            << "    cmp eax, 0DFFFh\n"
                            << "    ja ir_valid_char_" << item.output << "\n"
                            << "ir_invalid_char_" << item.output << ":\n"
                            << "    mov eax, 0FFFDh\n"
                            << "ir_valid_char_" << item.output << ":\n";
                    }
                    out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                }
            } else if constexpr (std::is_same_v<T, IrUnary>) {
                if (item.type == ValueType::Double) {
                    out << "    mov rax, qword [rbp-" << valueOffset(program, item.operand) << "]\n";
                    if (item.op == "-") out << "    btc rax, 63\n";
                    out << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rax\n";
                } else {
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.operand) << "]\n";
                    if (item.op == "-") out << "    neg eax\n";
                    if (item.op == "!") out << "    xor eax, 1\n";
                    if (item.type == ValueType::Byte) out << "    and eax, 255\n";
                    out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                }
            } else if constexpr (std::is_same_v<T, IrBinary>) {
                const bool comparison = item.op == "==" || item.op == "!=" ||
                    item.op == "<" || item.op == "<=" || item.op == ">" || item.op == ">=";
                if (item.op == "&&" || item.op == "||") {
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.left) << "]\n"
                        << "    " << (item.op == "&&" ? "and" : "or")
                        << " eax, dword [rbp-" << valueOffset(program, item.right) << "]\n"
                        << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                    return;
                }
                if ((item.op == "==" || item.op == "!=") &&
                    (item.operandType.kind == ValueType::Kind::Enum ||
                     item.operandType.kind == ValueType::Kind::Struct)) {
                    const std::string label = "ir_aggregate_equal_" +
                                              std::to_string(item.output);
                    const std::string different = label + "_different";
                    const std::string done = label + "_result_done";
                    emitEqualityChecks(out,
                        "[rbp-" + std::to_string(valueOffset(program, item.left)) + "]",
                        "[rbp-" + std::to_string(valueOffset(program, item.right)) + "]",
                        item.operandType, different, label);
                    out << "    mov eax, " << (item.op == "==" ? 1 : 0) << "\n"
                        << "    jmp " << done << "\n"
                        << different << ":\n"
                        << "    mov eax, " << (item.op == "==" ? 0 : 1) << "\n"
                        << done << ":\n"
                        << "    mov dword [rbp-" << valueOffset(program, item.output)
                        << "], eax\n";
                    return;
                }
                if (comparison && (item.operandType == ValueType::String ||
                                   item.operandType == ValueType::StringView)) {
                    const std::size_t left = valueOffset(program, item.left);
                    const std::size_t right = valueOffset(program, item.right);
                    out << "    mov rcx, qword [rbp-" << left - 8U << "]\n"
                        << "    cmp rcx, qword [rbp-" << right - 8U << "]\n"
                        << "    jne ir_string_different_" << item.output << "\n"
                        << "    mov rsi, qword [rbp-" << left << "]\n"
                        << "    mov rdi, qword [rbp-" << right << "]\n"
                        << "    test rcx, rcx\n"
                        << "    je ir_string_equal_" << item.output << "\n"
                        << "ir_string_compare_" << item.output << ":\n"
                        << "    mov al, byte [rsi]\n"
                        << "    cmp al, byte [rdi]\n"
                        << "    jne ir_string_different_" << item.output << "\n"
                        << "    inc rsi\n    inc rdi\n    dec rcx\n"
                        << "    jne ir_string_compare_" << item.output << "\n"
                        << "ir_string_equal_" << item.output << ":\n"
                        << "    mov eax, " << (item.op == "==" ? 1 : 0) << "\n"
                        << "    jmp ir_string_done_" << item.output << "\n"
                        << "ir_string_different_" << item.output << ":\n"
                        << "    mov eax, " << (item.op == "==" ? 0 : 1) << "\n"
                        << "ir_string_done_" << item.output << ":\n"
                        << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                    return;
                }
                if (comparison && item.operandType == ValueType::Double) {
                    out << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.left) << "]\n"
                        << "    ucomisd xmm0, qword [rbp-" << valueOffset(program, item.right) << "]\n";
                    if (item.op == "==") out << "    sete al\n    setnp dl\n    and al, dl\n";
                    if (item.op == "!=") out << "    setne al\n    setp dl\n    or al, dl\n";
                    if (item.op == "<") out << "    setb al\n    setnp dl\n    and al, dl\n";
                    if (item.op == ">") out << "    seta al\n";
                    if (item.op == "<=") out << "    setbe al\n    setnp dl\n    and al, dl\n";
                    if (item.op == ">=") out << "    setae al\n";
                    out << "    movzx eax, al\n"
                        << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                    return;
                }
                if (comparison) {
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.left) << "]\n"
                        << "    cmp eax, dword [rbp-" << valueOffset(program, item.right) << "]\n";
                    const bool unsignedComparison = item.operandType == ValueType::Byte ||
                                                    item.operandType == ValueType::Char;
                    const char* condition = item.op == "==" ? "e" : item.op == "!=" ? "ne" :
                        item.op == "<" ? (unsignedComparison ? "b" : "l") :
                        item.op == ">" ? (unsignedComparison ? "a" : "g") :
                        item.op == "<=" ? (unsignedComparison ? "be" : "le") :
                        (unsignedComparison ? "ae" : "ge");
                    out << "    set" << condition << " al\n"
                        << "    movzx eax, al\n"
                        << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                    return;
                }
                if (item.type == ValueType::Double) {
                    out << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.left) << "]\n";
                    const char* operation = item.op == "+" ? "addsd" : item.op == "-" ? "subsd" :
                                            item.op == "*" ? "mulsd" : "divsd";
                    out << "    " << operation << " xmm0, qword [rbp-"
                        << valueOffset(program, item.right) << "]\n"
                        << "    movsd qword [rbp-" << valueOffset(program, item.output) << "], xmm0\n";
                    return;
                }
                out << "    mov eax, dword [rbp-" << valueOffset(program, item.left) << "]\n";
                if (item.op == "+") out << "    add eax, dword [rbp-" << valueOffset(program, item.right) << "]\n";
                if (item.op == "-") out << "    sub eax, dword [rbp-" << valueOffset(program, item.right) << "]\n";
                if (item.op == "*") out << "    imul eax, dword [rbp-" << valueOffset(program, item.right) << "]\n";
                if (item.op == "/") {
                    if (item.type == ValueType::Byte)
                        out << "    xor edx, edx\n";
                    else
                        out << "    cdq\n";
                    out << "    mov ecx, dword [rbp-" << valueOffset(program, item.right) << "]\n"
                        << (item.type == ValueType::Byte ? "    div ecx\n" : "    idiv ecx\n");
                }
                if (item.type == ValueType::Byte) out << "    and eax, 255\n";
                out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrStore>) {
                const IrSlot& slot = program.slots[item.slot];
                const std::string address = slot.global
                    ? "[" + globalLabel(slot) + "]"
                    : "[rbp-" + std::to_string(slotOffset(program, item.slot)) + "]";
                if (item.type == ValueType::Unit) {
                    // Unit slots contain no payload.
                } else if (item.type.kind == ValueType::Kind::Array ||
                    isAggregateValue(item.type) ||
                    isPairValue(item.type)) {
                    emitBlockCopy(out,
                        "[rbp-" + std::to_string(valueOffset(program, item.value)) + "]",
                        address, valueTypeSize(item.type));
                } else if (item.type.kind == ValueType::Kind::Reference ||
                           item.type.kind == ValueType::Kind::Box) {
                    out << "    mov rax, qword [rbp-" << valueOffset(program, item.value) << "]\n"
                        << "    mov qword " << address << ", rax\n";
                } else if (item.type == ValueType::String) {
                    const std::size_t source = valueOffset(program, item.value);
                    out << "    mov rax, qword [rbp-" << source << "]\n"
                        << "    mov qword " << address << ", rax\n"
                        << "    mov rax, qword [rbp-" << source - 8U << "]\n"
                        << "    mov qword " << address.substr(0, address.size() - 1) << "+8], rax\n";
                } else if (item.type == ValueType::Double) {
                    out << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.value) << "]\n"
                        << "    movsd qword " << address << ", xmm0\n";
                } else {
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.value) << "]\n"
                        << "    mov " << (item.type == ValueType::Byte || item.type == ValueType::Bool ? "byte" : "dword")
                        << ' ' << address << ", "
                        << (item.type == ValueType::Byte || item.type == ValueType::Bool ? "al\n" : "eax\n");
                }
            } else if constexpr (std::is_same_v<T, IrCopy>) {
                if (item.type == ValueType::Unit) {
                    // Unit copies only connect SSA control-flow edges.
                } else if (item.type.kind == ValueType::Kind::Array ||
                    isAggregateValue(item.type) ||
                    isPairValue(item.type)) {
                    emitBlockCopy(out,
                        "[rbp-" + std::to_string(valueOffset(program, item.input)) + "]",
                        "[rbp-" + std::to_string(valueOffset(program, item.output)) + "]",
                        valueTypeSize(item.type));
                } else if (item.type.kind == ValueType::Kind::Reference ||
                           item.type.kind == ValueType::Kind::Box) {
                    out << "    mov rax, qword [rbp-" << valueOffset(program, item.input) << "]\n"
                        << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rax\n";
                } else if (item.type == ValueType::String) {
                    const std::size_t input = valueOffset(program, item.input);
                    const std::size_t output = valueOffset(program, item.output);
                    out << "    mov rax, qword [rbp-" << input << "]\n"
                        << "    mov qword [rbp-" << output << "], rax\n"
                        << "    mov rax, qword [rbp-" << input - 8U << "]\n"
                        << "    mov qword [rbp-" << output - 8U << "], rax\n";
                } else if (item.type == ValueType::Double) {
                    out << "    mov rax, qword [rbp-" << valueOffset(program, item.input) << "]\n"
                        << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rax\n";
                } else {
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.input) << "]\n"
                        << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                }
            } else if constexpr (std::is_same_v<T, IrCall>) {
                for (std::size_t i = item.arguments.size(); i-- > 0;) {
                    if (item.argumentTypes[i] == ValueType::Unit) {
                        out << "    push 0\n";
                    } else if (isAggregateValue(item.argumentTypes[i])) {
                        const std::size_t bytes = (valueTypeSize(item.argumentTypes[i]) + 7U) / 8U * 8U;
                        out << "    sub rsp, " << bytes << '\n';
                        emitBlockCopy(out, "[rbp-" + std::to_string(valueOffset(program, item.arguments[i])) + "]",
                            "[rsp]", valueTypeSize(item.argumentTypes[i]));
                    } else if (isPairValue(item.argumentTypes[i])) {
                        const std::size_t offset = valueOffset(program, item.arguments[i]);
                        out << "    push qword [rbp-" << offset - 8U << "]\n"
                            << "    push qword [rbp-" << offset << "]\n";
                    } else if (item.argumentTypes[i].kind == ValueType::Kind::Reference ||
                               item.argumentTypes[i].kind == ValueType::Kind::Box) {
                        out << "    push qword [rbp-"
                            << valueOffset(program, item.arguments[i]) << "]\n";
                    } else if (item.argumentTypes[i] == ValueType::Double) {
                        out << "    sub rsp, 8\n"
                            << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.arguments[i]) << "]\n"
                            << "    movsd qword [rsp], xmm0\n";
                    } else {
                        out << "    mov eax, dword [rbp-" << valueOffset(program, item.arguments[i]) << "]\n"
                            << "    push rax\n";
                    }
                }
                out << "    call zeta_fn_" << item.function << '\n';
                std::size_t argumentBytes = 0;
                for (ValueType type : item.argumentTypes)
                    argumentBytes += isAggregateValue(type)
                        ? (valueTypeSize(type) + 7U) / 8U * 8U : isPairValue(type) ? 16U : 8U;
                if (argumentBytes != 0) out << "    add rsp, " << argumentBytes << '\n';
                if (item.returnType == ValueType::Unit) {
                    // Unit-returning calls have no return register to preserve.
                } else if (isAggregateValue(item.returnType)) {
                    const std::size_t offset = valueOffset(program, item.output);
                    out << "    mov qword [rbp-" << offset << "], rax\n";
                    if (valueTypeSize(item.returnType) > 8U)
                        out << "    mov qword [rbp-" << offset << "+8], rdx\n";
                } else if (isPairValue(item.returnType)) {
                    const std::size_t offset = valueOffset(program, item.output);
                    out << "    mov qword [rbp-" << offset << "], rax\n"
                        << "    mov qword [rbp-" << offset - 8U << "], rdx\n";
                } else if (item.returnType.kind == ValueType::Kind::Box)
                    out << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rax\n";
                else if (item.returnType == ValueType::Double)
                    out << "    movsd qword [rbp-" << valueOffset(program, item.output) << "], xmm0\n";
                else
                    out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrTailCall>) {
                std::size_t stackOffset = 16U;
                for (std::size_t i = 0; i < item.arguments.size(); ++i) {
                    if (item.argumentTypes[i] == ValueType::Unit) {
                        // Unit occupies an ABI stack slot but has no payload.
                    } else if (isAggregateValue(item.argumentTypes[i])) {
                        emitBlockCopy(out,
                            "[rbp-" + std::to_string(valueOffset(program, item.arguments[i])) + "]",
                            "[rbp+" + std::to_string(stackOffset) + "]",
                            valueTypeSize(item.argumentTypes[i]));
                    } else if (isPairValue(item.argumentTypes[i])) {
                        const std::size_t source = valueOffset(program, item.arguments[i]);
                        out << "    mov rax, qword [rbp-" << source << "]\n"
                            << "    mov rdx, qword [rbp-" << source - 8U << "]\n"
                            << "    mov qword [rbp+" << stackOffset << "], rax\n"
                            << "    mov qword [rbp+" << stackOffset + 8U << "], rdx\n";
                    } else if (item.argumentTypes[i].kind == ValueType::Kind::Reference ||
                               item.argumentTypes[i].kind == ValueType::Kind::Box) {
                        out << "    mov rax, qword [rbp-"
                            << valueOffset(program, item.arguments[i]) << "]\n"
                            << "    mov qword [rbp+" << stackOffset << "], rax\n";
                    } else if (item.argumentTypes[i] == ValueType::Double) {
                        out << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.arguments[i]) << "]\n"
                            << "    movsd qword [rbp+" << stackOffset << "], xmm0\n";
                    } else {
                        out << "    mov eax, dword [rbp-" << valueOffset(program, item.arguments[i]) << "]\n"
                            << "    mov dword [rbp+" << stackOffset << "], eax\n";
                    }
                    stackOffset += isAggregateValue(item.argumentTypes[i])
                        ? (valueTypeSize(item.argumentTypes[i]) + 7U) / 8U * 8U
                        : isPairValue(item.argumentTypes[i]) ? 16U : 8U;
                }
                out << "    jmp zeta_fn_" << item.function << "_body\n";
            } else if constexpr (std::is_same_v<T, IrFunctionStart>) {
                out << "\nzeta_fn_" << item.name << ":\n"
                    << "    push rbp\n"
                    << "    mov rbp, rsp\n";
                if (frameSize != 0) out << "    sub rsp, " << frameSize << '\n';
                out << "zeta_fn_" << item.name << "_body:\n";
            } else if constexpr (std::is_same_v<T, IrParameter>) {
                if (item.type == ValueType::Unit) {
                    // Unit parameters have no payload.
                } else if (isAggregateValue(item.type)) {
                    emitBlockCopy(out, "[rbp+" + std::to_string(item.stackOffset) + "]",
                        "[rbp-" + std::to_string(valueOffset(program, item.output)) + "]", valueTypeSize(item.type));
                } else if (isPairValue(item.type)) {
                    const std::size_t offset = valueOffset(program, item.output);
                    out << "    mov rax, qword [rbp+" << item.stackOffset << "]\n"
                        << "    mov qword [rbp-" << offset << "], rax\n"
                        << "    mov rax, qword [rbp+" << item.stackOffset + 8U << "]\n"
                        << "    mov qword [rbp-" << offset - 8U << "], rax\n";
                } else if (item.type.kind == ValueType::Kind::Reference ||
                           item.type.kind == ValueType::Kind::Box) {
                    out << "    mov rax, qword [rbp+" << item.stackOffset << "]\n"
                        << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rax\n";
                } else if (item.type == ValueType::Double) {
                    out << "    movsd xmm0, qword [rbp+" << item.stackOffset << "]\n"
                        << "    movsd qword [rbp-" << valueOffset(program, item.output) << "], xmm0\n";
                } else {
                    out << "    mov eax, dword [rbp+" << item.stackOffset << "]\n"
                        << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                }
            } else if constexpr (std::is_same_v<T, IrReturn>) {
                if (item.type == ValueType::Unit) {
                    // Unit returns do not define a return register.
                } else if (isAggregateValue(item.type)) {
                    const std::size_t offset = valueOffset(program, item.value);
                    out << "    mov rax, qword [rbp-" << offset << "]\n";
                    if (valueTypeSize(item.type) > 8U)
                        out << "    mov rdx, qword [rbp-" << offset << "+8]\n";
                } else if (isPairValue(item.type)) {
                    const std::size_t offset = valueOffset(program, item.value);
                    out << "    mov rax, qword [rbp-" << offset << "]\n"
                        << "    mov rdx, qword [rbp-" << offset - 8U << "]\n";
                } else if (item.type.kind == ValueType::Kind::Box)
                    out << "    mov rax, qword [rbp-" << valueOffset(program, item.value) << "]\n";
                else if (item.type == ValueType::Double)
                    out << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.value) << "]\n";
                else
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.value) << "]\n";
                out << "    leave\n    ret\n";
            } else if constexpr (std::is_same_v<T, IrDrop>) {
                emitValueDrop(out,
                    "[rbp-" + std::to_string(valueOffset(program, item.value)) + "]",
                    item.type, "ir_drop_" + std::to_string(resourceSequence++));
            } else if constexpr (std::is_same_v<T, IrRetain>) {
                emitValueRetain(out,
                    "[rbp-" + std::to_string(valueOffset(program, item.value)) + "]",
                    item.type, "ir_retain_" + std::to_string(resourceSequence++));
            } else if constexpr (std::is_same_v<T, IrExit>) {
                out << "    mov edi, dword [rbp-" << valueOffset(program, item.value) << "]\n"
                       "    mov eax, 60\n"
                       "    syscall\n";
            } else if constexpr (std::is_same_v<T, IrBranch>) {
                out << "    cmp dword [rbp-" << valueOffset(program, item.condition) << "], 0\n"
                    << "    " << (item.jumpWhenTrue ? "jne" : "je")
                    << " ir_label_" << item.label << '\n';
            } else if constexpr (std::is_same_v<T, IrJump>) {
                out << "    jmp ir_label_" << item.label << '\n';
            } else {
                out << "ir_label_" << item.label << ":\n";
            }
        }, instruction);
    }

    bool hasArrayAccess = false;
    for (const IrInstruction& instruction : program.instructions)
        hasArrayAccess = hasArrayAccess || std::holds_alternative<IrIndexLoad>(instruction) ||
                         std::holds_alternative<IrIndexAddress>(instruction) ||
                         std::holds_alternative<IrIndexStore>(instruction) ||
                         std::holds_alternative<IrVecPopValue>(instruction) ||
                         std::holds_alternative<IrVecSet>(instruction);
    if (hasArrayAccess) {
        out << "\nir_array_bounds_error:\n"
               "    mov edi, 101\n"
               "    mov eax, 60\n"
               "    syscall\n";
    }
    if (std::any_of(program.instructions.begin(), program.instructions.end(),
                    [](const IrInstruction& instruction) {
                        return std::holds_alternative<IrBoxConstruct>(instruction);
    })) {
        out << "ir_box_allocation_error:\n"
               "    mov edi, 102\n"
               "    mov eax, 60\n"
               "    syscall\n";
    }
    if (std::any_of(program.instructions.begin(), program.instructions.end(),
                    [](const IrInstruction& instruction) {
                        return std::holds_alternative<IrVecReserve>(instruction);
                    })) {
        out << "ir_vec_allocation_error:\n"
               "    mov edi, 105\n"
               "    mov eax, 60\n"
               "    syscall\n";
    }

    if (std::any_of(program.instructions.begin(), program.instructions.end(),
        [](const IrInstruction& instruction) {
            if (std::holds_alternative<IrStringConcat>(instruction)) return true;
            const auto* conversion = std::get_if<IrConvert>(&instruction);
            return conversion != nullptr && conversion->target == ValueType::String &&
                (conversion->source == ValueType::Int || conversion->source == ValueType::Byte ||
                 conversion->source == ValueType::Char || conversion->source == ValueType::Double);
        })) {
        out << "ir_string_size_error:\n"
               "    mov edi, 103\n"
               "    mov eax, 60\n"
               "    syscall\n"
               "ir_string_allocation_error:\n"
               "    mov edi, 104\n"
               "    mov eax, 60\n"
               "    syscall\n";
    }

    bool hasDoubleConstants = false;
    bool hasStringConstants = false;
    bool hasBoolStringConversion = false;
    bool hasDoubleStringConversion = false;
    for (const IrInstruction& instruction : program.instructions)
        if (std::holds_alternative<IrDoubleConst>(instruction)) hasDoubleConstants = true;
        else if (std::holds_alternative<IrStringConst>(instruction)) hasStringConstants = true;
        else if (const auto* conversion = std::get_if<IrConvert>(&instruction);
                 conversion != nullptr && conversion->target == ValueType::String &&
                 conversion->source == ValueType::Bool) hasBoolStringConversion = true;
        else if (const auto* conversion = std::get_if<IrConvert>(&instruction);
                 conversion != nullptr && conversion->target == ValueType::String &&
                 conversion->source == ValueType::Double) hasDoubleStringConversion = true;
    bool hasGlobals = false;
    for (const IrSlot& slot : program.slots) hasGlobals = hasGlobals || slot.global;
    if (hasDoubleConstants || hasStringConstants || hasBoolStringConversion ||
        hasDoubleStringConversion || hasGlobals) {
        out << "\nsegment readable" << (hasGlobals ? " writeable" : "") << "\n";
        for (const IrInstruction& instruction : program.instructions) {
            if (const auto* item = std::get_if<IrDoubleConst>(&instruction))
                out << "double_const_" << item->output << ": dq "
                    << formatDouble(item->value) << '\n';
            if (const auto* item = std::get_if<IrStringConst>(&instruction)) {
                out << "string_object_" << item->output << ":\n"
                    << "    dq -1\n"
                    << "    dq " << item->utf8.size() << "\n"
                    << "string_const_" << item->output << ": db ";
                if (item->utf8.empty()) out << '0';
                else for (std::size_t i = 0; i < item->utf8.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << static_cast<unsigned>(static_cast<unsigned char>(item->utf8[i]));
                }
                out << '\n';
            }
        }
        if (hasBoolStringConversion) {
            out << "string_bool_true_object: dq -1, 4\n"
                   "string_bool_true: db 'true'\n"
                   "string_bool_false_object: dq -1, 5\n"
                   "string_bool_false: db 'false'\n";
        }
        if (hasDoubleStringConversion)
            out << "string_double_million: dq 1000000.0\n"
                   "string_double_half: dq 0.5\n";
        for (std::size_t i = 0; i < program.slots.size(); ++i) {
            if (!program.slots[i].global || program.slots[i].external) continue;
            out << globalLabel(program.slots[i]) << ": ";
            if (program.slots[i].type.kind == ValueType::Kind::Array)
                out << "rb " << valueTypeSize(program.slots[i].type) << '\n';
            else out << (program.slots[i].type == ValueType::String ? "rq 2" :
                    program.slots[i].type == ValueType::Double ? "rq 1" :
                    (program.slots[i].type == ValueType::Int ||
                     program.slots[i].type == ValueType::Char) ? "rd 1" : "rb 1") << '\n';
        }
    }
    return out.str();
}

std::string FasmCodeGenerator::generateObject(
    const IrProgram& program, bool entryPoint, const std::string& initializer) {
    return generateObject(IrVerifier::verify(
        program, entryPoint ? IrVerificationMode::Executable
                            : IrVerificationMode::ModuleObject),
        entryPoint, initializer);
}

std::string FasmCodeGenerator::generateObject(
    const VerifiedIrProgram& verified, bool entryPoint, const std::string& initializer) {
    const IrVerificationMode expected = entryPoint ? IrVerificationMode::Executable
                                                   : IrVerificationMode::ModuleObject;
    if (verified.mode() != expected)
        throw IrVerificationError("IRV004", "mode de vérification incompatible avec l'objet");
    const IrProgram& program = verified.program();
    std::string assembly = generateUnchecked(program);
    const std::string executableHeader =
        "format ELF64 executable 3\nentry start\n\nsegment readable executable\n";
    assembly.replace(0, executableHeader.size(),
                     "format ELF64\nsection '.text' executable\n" +
                     std::string(entryPoint ? "public start\n" :
                         initializer.empty() ? "" : "public zeta_init_" + initializer + "\n"));
    if (!initializer.empty()) {
        assembly.replace(assembly.find("start:"), 6U, "zeta_init_" + initializer + ":");
        const std::string initializerLabel = "zeta_init_" + initializer + ":\n";
        assembly.insert(assembly.find(initializerLabel) + initializerLabel.size(), "    push rbp\n");
        if (const std::size_t firstFunction = assembly.find("\nzeta_fn_");
            firstFunction != std::string::npos)
            assembly.insert(firstFunction, "    leave\n    ret\n");
        else
            assembly += "    leave\n    ret\n";
    }
    std::set<std::string> definitions;
    std::set<std::string> calls;
    for (const IrInstruction& instruction : program.instructions) {
        if (const auto* function = std::get_if<IrFunctionStart>(&instruction))
            definitions.insert(function->name);
        else if (const auto* call = std::get_if<IrCall>(&instruction))
            calls.insert(call->function);
        else if (const auto* call = std::get_if<IrTailCall>(&instruction))
            calls.insert(call->function);
    }
    std::string externals;
    for (const std::string& definition : definitions)
        externals += "public zeta_fn_" + definition + "\n";
    for (const std::string& call : calls)
        if (!definitions.contains(call)) externals += "extrn zeta_fn_" + call + "\n";
    for (const IrSlot& slot : program.slots) {
        if (!slot.global) continue;
        externals += std::string(slot.external ? "extrn " : "public ") +
            globalLabel(slot) + "\n";
    }
    const std::string entryLabel = initializer.empty() ? "start:" : "zeta_init_" + initializer + ":";
    assembly.insert(assembly.find(entryLabel), externals);
    const auto replaceSegment = [&](const std::string& from, const std::string& to) {
        if (const std::size_t position = assembly.find(from); position != std::string::npos)
            assembly.replace(position, from.size(), to);
    };
    replaceSegment("\nsegment readable writeable\n", "\nsection '.data' writeable\n");
    replaceSegment("\nsegment readable\n", "\nsection '.rodata'\n");
    return assembly;
}

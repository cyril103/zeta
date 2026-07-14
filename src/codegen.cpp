#include "codegen.hpp"
#include "ir_verifier.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <set>
#include <type_traits>

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
    if (target.reference) return "[rbx]";
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
                const std::size_t vector = valueOffset(program, item.vector);
                const std::size_t output = valueOffset(program, item.output);
                if (item.property == "length")
                    out << "    mov eax, dword [rbp-" << vector - 8U << "]\n";
                else if (item.property == "capacity")
                    out << "    mov eax, dword [rbp-" << vector - 16U << "]\n";
                else
                    out << "    cmp qword [rbp-" << vector - 8U << "], 0\n"
                        << "    sete al\n"
                        << "    movzx eax, al\n";
                out << "    mov dword [rbp-" << output << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrVecReserve>) {
                const std::size_t id = resourceSequence++;
                const std::size_t output = valueOffset(program, item.output);
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
                    << "    mov dword [rbp-" << output << "], 0\n"
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
                out << "    inc qword " << displacedAddress(address, 8U) << "\n"
                    << "    mov dword [rbp-" << valueOffset(program, item.output) << "], 0\n";
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
                out << "    mov qword " << displacedAddress(address, 8U) << ", 0\n"
                    << "    mov dword [rbp-" << valueOffset(program, item.output) << "], 0\n";
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
                const IrSlot& slot = program.slots[item.slot];
                const std::string address = slot.global
                    ? "[" + globalLabel(slot) + "]"
                    : "[rbp-" + std::to_string(slotOffset(program, item.slot)) + "]";
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
            } else if constexpr (std::is_same_v<T, IrVecPop>) {
                const std::size_t id = resourceSequence++;
                const IrSlot& slot = program.slots[item.slot];
                const std::string address = slot.global
                    ? "[" + globalLabel(slot) + "]"
                    : "[rbp-" + std::to_string(slotOffset(program, item.slot)) + "]";
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
                out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], 0\n"
                    << "    pop r12\n";
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
                         std::holds_alternative<IrIndexStore>(instruction) ||
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

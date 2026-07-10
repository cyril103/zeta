#include "codegen.hpp"

#include <sstream>
#include <type_traits>

namespace {
std::size_t align16(std::size_t value) { return (value + 15U) & ~std::size_t{15U}; }
std::size_t typeSize(ValueType type) { return type == ValueType::Int ? 4U : 1U; }
std::size_t slotBytes(const IrProgram& program) {
    std::size_t size = 0;
    for (const IrSlot& slot : program.slots) size += typeSize(slot.type);
    return size;
}
std::size_t slotOffset(const IrProgram& program, SlotId target) {
    std::size_t offset = 0;
    for (SlotId slot = 0; slot <= target; ++slot) offset += typeSize(program.slots[slot].type);
    return offset;
}
std::size_t valueOffset(const IrProgram& program, ValueId value) {
    return slotBytes(program) + (value + 1U) * 4U;
}
}

std::string FasmCodeGenerator::generate(const IrProgram& program) {
    const std::size_t frameSize = align16(slotBytes(program) + program.valueCount * 4U);
    std::ostringstream out;
    out << "format ELF64 executable 3\n"
           "entry start\n\n"
           "segment readable executable\n"
           "start:\n"
           "    mov rbp, rsp\n";
    if (frameSize != 0) out << "    sub rsp, " << frameSize << '\n';

    for (const IrInstruction& instruction : program.instructions) {
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, IrConst>) {
                out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], " << item.value << '\n';
            } else if constexpr (std::is_same_v<T, IrLoad>) {
                if (item.type == ValueType::Byte)
                    out << "    movzx eax, byte [rbp-" << slotOffset(program, item.slot) << "]\n";
                else
                    out << "    mov eax, dword [rbp-" << slotOffset(program, item.slot) << "]\n";
                out
                    << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrUnary>) {
                out << "    mov eax, dword [rbp-" << valueOffset(program, item.operand) << "]\n";
                if (item.op == '-') out << "    neg eax\n";
                if (item.type == ValueType::Byte) out << "    and eax, 255\n";
                out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrBinary>) {
                out << "    mov eax, dword [rbp-" << valueOffset(program, item.left) << "]\n";
                if (item.op == '+') out << "    add eax, dword [rbp-" << valueOffset(program, item.right) << "]\n";
                if (item.op == '-') out << "    sub eax, dword [rbp-" << valueOffset(program, item.right) << "]\n";
                if (item.op == '*') out << "    imul eax, dword [rbp-" << valueOffset(program, item.right) << "]\n";
                if (item.op == '/') {
                    if (item.type == ValueType::Byte)
                        out << "    xor edx, edx\n";
                    else
                        out << "    cdq\n";
                    out << "    mov ecx, dword [rbp-" << valueOffset(program, item.right) << "]\n"
                        << (item.type == ValueType::Byte ? "    div ecx\n" : "    idiv ecx\n");
                }
                if (item.type == ValueType::Byte) out << "    and eax, 255\n";
                out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else {
                out << "    mov eax, dword [rbp-" << valueOffset(program, item.value) << "]\n"
                    << "    mov " << (item.type == ValueType::Byte ? "byte" : "dword")
                    << " [rbp-" << slotOffset(program, item.slot) << "], "
                    << (item.type == ValueType::Byte ? "al\n" : "eax\n");
            }
        }, instruction);
    }

    out << "    mov edi, dword [rbp-" << valueOffset(program, program.exitValue) << "]\n"
           "    mov eax, 60\n"
           "    syscall\n";
    return out.str();
}

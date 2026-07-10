#include "codegen.hpp"

#include <sstream>
#include <type_traits>

namespace {
std::size_t align16(std::size_t value) { return (value + 15U) & ~std::size_t{15U}; }
std::size_t slotOffset(SlotId slot) { return (slot + 1U) * 4U; }
std::size_t valueOffset(const IrProgram& program, ValueId value) {
    return (program.slots.size() + value + 1U) * 4U;
}
}

std::string FasmCodeGenerator::generate(const IrProgram& program) {
    const std::size_t frameSize = align16((program.slots.size() + program.valueCount) * 4U);
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
                out << "    mov eax, dword [rbp-" << slotOffset(item.slot) << "]\n"
                    << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrUnary>) {
                out << "    mov eax, dword [rbp-" << valueOffset(program, item.operand) << "]\n";
                if (item.op == '-') out << "    neg eax\n";
                out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrBinary>) {
                out << "    mov eax, dword [rbp-" << valueOffset(program, item.left) << "]\n";
                if (item.op == '+') out << "    add eax, dword [rbp-" << valueOffset(program, item.right) << "]\n";
                if (item.op == '-') out << "    sub eax, dword [rbp-" << valueOffset(program, item.right) << "]\n";
                if (item.op == '*') out << "    imul eax, dword [rbp-" << valueOffset(program, item.right) << "]\n";
                if (item.op == '/') {
                    out << "    cdq\n"
                        << "    mov ecx, dword [rbp-" << valueOffset(program, item.right) << "]\n"
                        << "    idiv ecx\n";
                }
                out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else {
                out << "    mov eax, dword [rbp-" << valueOffset(program, item.value) << "]\n"
                    << "    mov dword [rbp-" << slotOffset(item.slot) << "], eax\n";
            }
        }, instruction);
    }

    out << "    mov eax, 60\n"
           "    xor edi, edi\n"
           "    syscall\n";
    return out.str();
}

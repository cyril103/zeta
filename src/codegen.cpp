#include "codegen.hpp"

#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace {
std::size_t align16(std::size_t value) { return (value + 15U) & ~std::size_t{15U}; }
std::size_t typeSize(ValueType type) {
    if (type == ValueType::Byte || type == ValueType::Bool) return 1U;
    return type == ValueType::Double ? 8U : 4U;
}
std::size_t valueSize(ValueType type) { return type == ValueType::Double ? 8U : 4U; }
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
}

std::string FasmCodeGenerator::generate(const IrProgram& program) {
    const std::size_t frameSize = align16(slotBytes(program) + valueBytes(program));
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
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
            } else if constexpr (std::is_same_v<T, IrDoubleConst>) {
                out << "    movsd xmm0, qword [double_const_" << item.output << "]\n"
                    << "    movsd qword [rbp-" << valueOffset(program, item.output) << "], xmm0\n";
            } else if constexpr (std::is_same_v<T, IrLoad>) {
                const IrSlot& slot = program.slots[item.slot];
                const std::string address = slot.global
                    ? "[global_slot_" + std::to_string(item.slot) + "]"
                    : "[rbp-" + std::to_string(slotOffset(program, item.slot)) + "]";
                if (item.type == ValueType::Double) {
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
                if (item.source == item.target) {
                    if (item.target == ValueType::Double) {
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
                    const bool unsignedComparison = item.operandType == ValueType::Byte;
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
                    ? "[global_slot_" + std::to_string(item.slot) + "]"
                    : "[rbp-" + std::to_string(slotOffset(program, item.slot)) + "]";
                if (item.type == ValueType::Double) {
                    out << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.value) << "]\n"
                        << "    movsd qword " << address << ", xmm0\n";
                } else {
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.value) << "]\n"
                        << "    mov " << (item.type == ValueType::Byte || item.type == ValueType::Bool ? "byte" : "dword")
                        << ' ' << address << ", "
                        << (item.type == ValueType::Byte || item.type == ValueType::Bool ? "al\n" : "eax\n");
                }
            } else if constexpr (std::is_same_v<T, IrCopy>) {
                if (item.type == ValueType::Double) {
                    out << "    mov rax, qword [rbp-" << valueOffset(program, item.input) << "]\n"
                        << "    mov qword [rbp-" << valueOffset(program, item.output) << "], rax\n";
                } else {
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.input) << "]\n"
                        << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                }
            } else if constexpr (std::is_same_v<T, IrCall>) {
                for (std::size_t i = item.arguments.size(); i-- > 0;) {
                    if (item.argumentTypes[i] == ValueType::Double) {
                        out << "    sub rsp, 8\n"
                            << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.arguments[i]) << "]\n"
                            << "    movsd qword [rsp], xmm0\n";
                    } else {
                        out << "    mov eax, dword [rbp-" << valueOffset(program, item.arguments[i]) << "]\n"
                            << "    push rax\n";
                    }
                }
                out << "    call zeta_fn_" << item.function << '\n';
                if (!item.arguments.empty()) out << "    add rsp, " << item.arguments.size() * 8U << '\n';
                if (item.returnType == ValueType::Double)
                    out << "    movsd qword [rbp-" << valueOffset(program, item.output) << "], xmm0\n";
                else
                    out << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
            } else if constexpr (std::is_same_v<T, IrTailCall>) {
                for (std::size_t i = 0; i < item.arguments.size(); ++i) {
                    if (item.argumentTypes[i] == ValueType::Double) {
                        out << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.arguments[i]) << "]\n"
                            << "    movsd qword [rbp+" << 16U + i * 8U << "], xmm0\n";
                    } else {
                        out << "    mov eax, dword [rbp-" << valueOffset(program, item.arguments[i]) << "]\n"
                            << "    mov dword [rbp+" << 16U + i * 8U << "], eax\n";
                    }
                }
                out << "    jmp zeta_fn_" << item.function << "_body\n";
            } else if constexpr (std::is_same_v<T, IrFunctionStart>) {
                out << "\nzeta_fn_" << item.name << ":\n"
                    << "    push rbp\n"
                    << "    mov rbp, rsp\n";
                if (frameSize != 0) out << "    sub rsp, " << frameSize << '\n';
                out << "zeta_fn_" << item.name << "_body:\n";
            } else if constexpr (std::is_same_v<T, IrParameter>) {
                if (item.type == ValueType::Double) {
                    out << "    movsd xmm0, qword [rbp+" << 16U + item.index * 8U << "]\n"
                        << "    movsd qword [rbp-" << valueOffset(program, item.output) << "], xmm0\n";
                } else {
                    out << "    mov eax, dword [rbp+" << 16U + item.index * 8U << "]\n"
                        << "    mov dword [rbp-" << valueOffset(program, item.output) << "], eax\n";
                }
            } else if constexpr (std::is_same_v<T, IrReturn>) {
                if (item.type == ValueType::Double)
                    out << "    movsd xmm0, qword [rbp-" << valueOffset(program, item.value) << "]\n";
                else
                    out << "    mov eax, dword [rbp-" << valueOffset(program, item.value) << "]\n";
                out << "    leave\n    ret\n";
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

    bool hasDoubleConstants = false;
    for (const IrInstruction& instruction : program.instructions)
        hasDoubleConstants = hasDoubleConstants || std::holds_alternative<IrDoubleConst>(instruction);
    bool hasGlobals = false;
    for (const IrSlot& slot : program.slots) hasGlobals = hasGlobals || slot.global;
    if (hasDoubleConstants || hasGlobals) {
        out << "\nsegment readable" << (hasGlobals ? " writeable" : "") << "\n";
        for (const IrInstruction& instruction : program.instructions) {
            if (const auto* item = std::get_if<IrDoubleConst>(&instruction))
                out << "double_const_" << item->output << ": dq "
                    << formatDouble(item->value) << '\n';
        }
        for (std::size_t i = 0; i < program.slots.size(); ++i) {
            if (!program.slots[i].global) continue;
            out << "global_slot_" << i << ": "
                << (program.slots[i].type == ValueType::Double ? "rq 1" :
                    program.slots[i].type == ValueType::Int ? "rd 1" : "rb 1") << '\n';
        }
    }
    return out.str();
}

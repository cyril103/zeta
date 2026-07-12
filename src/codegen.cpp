#include "codegen.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <set>
#include <type_traits>

namespace {
std::size_t align16(std::size_t value) { return (value + 15U) & ~std::size_t{15U}; }
bool isPairValue(ValueType type) {
    return type == ValueType::String || type.kind == ValueType::Kind::Slice;
}
std::size_t typeSize(ValueType type) {
    return valueTypeSize(type);
}
std::size_t valueSize(ValueType type) {
    if (type.kind == ValueType::Kind::Array || type.kind == ValueType::Kind::Struct) return valueTypeSize(type);
    if (type.kind == ValueType::Kind::Reference || type.kind == ValueType::Kind::Box) return 8U;
    if (type == ValueType::String || type.kind == ValueType::Kind::Slice) return 16U;
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
void emitBoxDrop(std::ostringstream& out, const std::string& source,
                 const ValueType& boxType) {
    out << "    mov rdi, " << source << "\n";
    if (boxType.element->kind == ValueType::Kind::Box) {
        out << "    push rdi\n"
            << "    mov rax, qword [rdi]\n";
        emitBoxDrop(out, "rax", *boxType.element);
        out << "    pop rdi\n";
    }
    out << "    mov rsi, " << valueTypeSize(*boxType.element) << "\n"
        << "    mov eax, 11\n"
        << "    syscall\n";
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
            } else if constexpr (std::is_same_v<T, IrStringConst>) {
                const std::size_t offset = valueOffset(program, item.output);
                out << "    lea rax, [string_const_" << item.output << "]\n"
                    << "    mov qword [rbp-" << offset << "], rax\n"
                    << "    mov qword [rbp-" << offset - 8U << "], " << item.utf8.size() << '\n';
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
            } else if constexpr (std::is_same_v<T, IrStructConstruct>) {
                const std::size_t output = valueOffset(program, item.output);
                for (std::size_t i = 0; i < item.fields.size(); ++i) {
                    const StructField& field = item.type.structure->fields[i];
                    emitBlockCopy(out, "[rbp-" + std::to_string(valueOffset(program, item.fields[i])) + "]",
                        "[rbp-" + std::to_string(output) + "+" + std::to_string(field.offset) + "]",
                        valueTypeSize(field.type));
                }
            } else if constexpr (std::is_same_v<T, IrFieldLoad>) {
                const StructField& field = item.objectType.structure->fields[item.field];
                emitBlockCopy(out, "[rbp-" + std::to_string(valueOffset(program, item.object)) + "+" + std::to_string(field.offset) + "]",
                    "[rbp-" + std::to_string(valueOffset(program, item.output)) + "]", valueTypeSize(field.type));
            } else if constexpr (std::is_same_v<T, IrSliceConstruct>) {
                const std::size_t output = valueOffset(program, item.output);
                out << "    mov rax, qword [rbp-" << valueOffset(program, item.reference) << "]\n"
                    << "    mov qword [rbp-" << output << "], rax\n"
                    << "    mov qword [rbp-" << output - 8U << "], " << item.length << "\n";
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
                        out << "    lea rdi, [global_slot_" << item.slot << "]\n";
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
                    out << "    lea rax, [global_slot_" << item.slot << "]\n";
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
                    ? "[global_slot_" + std::to_string(item.slot) + "]"
                    : "[rbp-" + std::to_string(slotOffset(program, item.slot)) + "]";
                if (item.type.kind == ValueType::Kind::Array ||
                    item.type.kind == ValueType::Kind::Struct ||
                    item.type.kind == ValueType::Kind::Slice) {
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
                if (item.source == item.target) {
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
                if (comparison && item.operandType == ValueType::String) {
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
                    ? "[global_slot_" + std::to_string(item.slot) + "]"
                    : "[rbp-" + std::to_string(slotOffset(program, item.slot)) + "]";
                if (item.type.kind == ValueType::Kind::Array ||
                    item.type.kind == ValueType::Kind::Struct ||
                    item.type.kind == ValueType::Kind::Slice) {
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
                if (item.type.kind == ValueType::Kind::Array ||
                    item.type.kind == ValueType::Kind::Struct ||
                    item.type.kind == ValueType::Kind::Slice) {
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
                    if (isPairValue(item.argumentTypes[i])) {
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
                    argumentBytes += isPairValue(type) ? 16U : 8U;
                if (argumentBytes != 0) out << "    add rsp, " << argumentBytes << '\n';
                if (item.returnType == ValueType::String) {
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
                    if (isPairValue(item.argumentTypes[i])) {
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
                    stackOffset += isPairValue(item.argumentTypes[i]) ? 16U : 8U;
                }
                out << "    jmp zeta_fn_" << item.function << "_body\n";
            } else if constexpr (std::is_same_v<T, IrFunctionStart>) {
                out << "\nzeta_fn_" << item.name << ":\n"
                    << "    push rbp\n"
                    << "    mov rbp, rsp\n";
                if (frameSize != 0) out << "    sub rsp, " << frameSize << '\n';
                out << "zeta_fn_" << item.name << "_body:\n";
            } else if constexpr (std::is_same_v<T, IrParameter>) {
                if (isPairValue(item.type)) {
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
                if (item.type == ValueType::String) {
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
                emitBoxDrop(out,
                    "qword [rbp-" + std::to_string(valueOffset(program, item.value)) + "]",
                    item.type);
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
                         std::holds_alternative<IrIndexStore>(instruction);
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

    bool hasDoubleConstants = false;
    bool hasStringConstants = false;
    for (const IrInstruction& instruction : program.instructions)
        if (std::holds_alternative<IrDoubleConst>(instruction)) hasDoubleConstants = true;
        else if (std::holds_alternative<IrStringConst>(instruction)) hasStringConstants = true;
    bool hasGlobals = false;
    for (const IrSlot& slot : program.slots) hasGlobals = hasGlobals || slot.global;
    if (hasDoubleConstants || hasStringConstants || hasGlobals) {
        out << "\nsegment readable" << (hasGlobals ? " writeable" : "") << "\n";
        for (const IrInstruction& instruction : program.instructions) {
            if (const auto* item = std::get_if<IrDoubleConst>(&instruction))
                out << "double_const_" << item->output << ": dq "
                    << formatDouble(item->value) << '\n';
            if (const auto* item = std::get_if<IrStringConst>(&instruction)) {
                out << "string_const_" << item->output << ": db ";
                if (item->utf8.empty()) out << '0';
                else for (std::size_t i = 0; i < item->utf8.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << static_cast<unsigned>(static_cast<unsigned char>(item->utf8[i]));
                }
                out << '\n';
            }
        }
        for (std::size_t i = 0; i < program.slots.size(); ++i) {
            if (!program.slots[i].global) continue;
            out << "global_slot_" << i << ": ";
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

std::string FasmCodeGenerator::generateObject(const IrProgram& program) {
    std::string assembly = generate(program);
    const std::string executableHeader =
        "format ELF64 executable 3\nentry start\n\nsegment readable executable\n";
    assembly.replace(0, executableHeader.size(),
                     "format ELF64\nsection '.text' executable\npublic start\n");
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
    for (const std::string& call : calls)
        if (!definitions.contains(call)) externals += "extrn zeta_fn_" + call + "\n";
    assembly.insert(assembly.find("start:"), externals);
    const auto replaceSegment = [&](const std::string& from, const std::string& to) {
        if (const std::size_t position = assembly.find(from); position != std::string::npos)
            assembly.replace(position, from.size(), to);
    };
    replaceSegment("\nsegment readable writeable\n", "\nsection '.data' writeable\n");
    replaceSegment("\nsegment readable\n", "\nsection '.rodata'\n");
    return assembly;
}

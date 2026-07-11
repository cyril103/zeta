format ELF64

section '.text' executable

public zeta_fn_io__print
public zeta_fn_io__println

; ABI Zeta : une String est passée sur la pile sous la forme {adresse, longueur}.
zeta_fn_io__print:
    mov rsi, qword [rsp+8]
    mov rdx, qword [rsp+16]
    xor r8d, r8d
.write:
    test rdx, rdx
    jz .done
    mov eax, 1
    mov edi, 1
    syscall
    test rax, rax
    jg .written
    cmp rax, -4
    je .write
    ret
.written:
    add r8, rax
    add rsi, rax
    sub rdx, rax
    jmp .write
.done:
    mov rax, r8
    ret

zeta_fn_io__println:
    xor eax, eax
    ret

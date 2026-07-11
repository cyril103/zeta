format ELF64

section '.text' executable

public zeta_fn_io__print
public zeta_fn_io__println

; ABI Zeta : une String est passée sur la pile sous la forme {adresse, longueur}.
zeta_fn_io__print:
    mov rsi, qword [rsp+8]
    mov rdx, qword [rsp+16]
    jmp io_write_all

zeta_fn_io__println:
    mov rsi, qword [rsp+8]
    mov rdx, qword [rsp+16]
    call io_write_all
    test rax, rax
    js .return
    mov r9, rax
    lea rsi, [io_newline]
    mov edx, 1
    call io_write_all
    test rax, rax
    js .return
    add rax, r9
.return:
    ret

io_write_all:
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

section '.rodata'
io_newline: db 10

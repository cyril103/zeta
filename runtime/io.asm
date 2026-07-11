format ELF64

section '.text' executable

public zeta_fn_io__print
public zeta_fn_io__println
public zeta_fn_io__printChar
public zeta_fn_io__printInt
public zeta_fn_io__printlnInt

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

zeta_fn_io__printChar:
    sub rsp, 8
    mov r10d, dword [rsp+16]
    cmp r10d, 7Fh
    ja .two
    mov byte [rsp], r10b
    mov edx, 1
    jmp .write
.two:
    cmp r10d, 7FFh
    ja .three
    mov eax, r10d
    shr eax, 6
    or al, 0C0h
    mov byte [rsp], al
    mov eax, r10d
    and al, 3Fh
    or al, 80h
    mov byte [rsp+1], al
    mov edx, 2
    jmp .write
.three:
    cmp r10d, 0FFFFh
    ja .four
    mov eax, r10d
    shr eax, 12
    or al, 0E0h
    mov byte [rsp], al
    mov eax, r10d
    shr eax, 6
    and al, 3Fh
    or al, 80h
    mov byte [rsp+1], al
    mov eax, r10d
    and al, 3Fh
    or al, 80h
    mov byte [rsp+2], al
    mov edx, 3
    jmp .write
.four:
    mov eax, r10d
    shr eax, 18
    or al, 0F0h
    mov byte [rsp], al
    mov eax, r10d
    shr eax, 12
    and al, 3Fh
    or al, 80h
    mov byte [rsp+1], al
    mov eax, r10d
    shr eax, 6
    and al, 3Fh
    or al, 80h
    mov byte [rsp+2], al
    mov eax, r10d
    and al, 3Fh
    or al, 80h
    mov byte [rsp+3], al
    mov edx, 4
.write:
    mov rsi, rsp
    call io_write_all
    add rsp, 8
    ret

zeta_fn_io__printInt:
    movsxd rax, dword [rsp+8]
    sub rsp, 16
    xor r10d, r10d
    xor r11d, r11d
    test rax, rax
    jns .magnitude
    neg rax
    mov r11d, 1
.magnitude:
    lea rsi, [rsp+16]
    test rax, rax
    jnz .digits
    dec rsi
    mov byte [rsi], '0'
    inc r10d
    jmp .sign
.digits:
    xor edx, edx
    mov ecx, 10
    div rcx
    add dl, '0'
    dec rsi
    mov byte [rsi], dl
    inc r10d
    test rax, rax
    jnz .digits
.sign:
    test r11d, r11d
    jz .write
    dec rsi
    mov byte [rsi], '-'
    inc r10d
.write:
    mov edx, r10d
    call io_write_all
    add rsp, 16
    ret

zeta_fn_io__printlnInt:
    movsxd rax, dword [rsp+8]
    push rax
    call zeta_fn_io__printInt
    add rsp, 8
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

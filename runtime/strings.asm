format ELF64

section '.text' executable

public zeta_fn_strings__decodeAtByte
public zeta_fn_strings__view
public zeta_fn_strings__viewIsValid

; Retourne {0, 0} si les bornes ne satisfont pas 0 <= début <= fin <= longueur.
zeta_fn_strings__view:
    mov rax, qword [rsp+8]
    mov r8, qword [rsp+16]
    movsxd rcx, dword [rsp+24]
    movsxd rdx, dword [rsp+32]
    test rcx, rcx
    js .invalid_view
    cmp rdx, rcx
    jl .invalid_view
    cmp rdx, r8
    jg .invalid_view
    add rax, rcx
    sub rdx, rcx
    ret
.invalid_view:
    xor eax, eax
    xor edx, edx
    ret

zeta_fn_strings__viewIsValid:
    xor eax, eax
    cmp qword [rsp+8], 0
    setne al
    ret

; String {adresse, longueur}, puis offset Int. Retourne le point de code ou -1.
zeta_fn_strings__decodeAtByte:
    mov rdx, qword [rsp+8]
    mov r8, qword [rsp+16]
    movsxd rcx, dword [rsp+24]
    test rcx, rcx
    js .invalid
    cmp rcx, r8
    jae .invalid
    movzx eax, byte [rdx+rcx]
    cmp eax, 7Fh
    jbe .return
    cmp eax, 0C2h
    jb .invalid
    cmp eax, 0DFh
    jbe .two
    cmp eax, 0EFh
    jbe .three
    cmp eax, 0F4h
    jbe .four
    jmp .invalid
.two:
    lea r9, [rcx+1]
    cmp r9, r8
    jae .invalid
    movzx r9d, byte [rdx+rcx+1]
    mov r10d, r9d
    and r10d, 0C0h
    cmp r10d, 080h
    jne .invalid
    and eax, 01Fh
    shl eax, 6
    and r9d, 03Fh
    or eax, r9d
    ret
.three:
    lea r9, [rcx+2]
    cmp r9, r8
    jae .invalid
    movzx r9d, byte [rdx+rcx+1]
    movzx r10d, byte [rdx+rcx+2]
    mov r11d, r9d
    and r11d, 0C0h
    cmp r11d, 080h
    jne .invalid
    mov r11d, r10d
    and r11d, 0C0h
    cmp r11d, 080h
    jne .invalid
    cmp al, 0E0h
    jne .not_e0
    cmp r9b, 0A0h
    jb .invalid
.not_e0:
    cmp al, 0EDh
    jne .three_decode
    cmp r9b, 09Fh
    ja .invalid
.three_decode:
    and eax, 00Fh
    shl eax, 12
    and r9d, 03Fh
    shl r9d, 6
    or eax, r9d
    and r10d, 03Fh
    or eax, r10d
    ret
.four:
    lea r9, [rcx+3]
    cmp r9, r8
    jae .invalid
    movzx r9d, byte [rdx+rcx+1]
    movzx r10d, byte [rdx+rcx+2]
    movzx r11d, byte [rdx+rcx+3]
    push r11
    and r11d, 0C0h
    cmp r11d, 080h
    jne .invalid_pop
    mov r11d, r9d
    and r11d, 0C0h
    cmp r11d, 080h
    jne .invalid_pop
    mov r11d, r10d
    and r11d, 0C0h
    cmp r11d, 080h
    jne .invalid_pop
    cmp al, 0F0h
    jne .not_f0
    cmp r9b, 090h
    jb .invalid_pop
.not_f0:
    cmp al, 0F4h
    jne .four_decode
    cmp r9b, 08Fh
    ja .invalid_pop
.four_decode:
    and eax, 007h
    shl eax, 18
    and r9d, 03Fh
    shl r9d, 12
    or eax, r9d
    and r10d, 03Fh
    shl r10d, 6
    or eax, r10d
    pop r11
    and r11d, 03Fh
    or eax, r11d
    ret
.invalid_pop:
    pop r11
.invalid:
    mov eax, -1
.return:
    ret

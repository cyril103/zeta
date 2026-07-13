format ELF64

section '.text' executable

public zeta_fn_io__print
public zeta_fn_io__println
public zeta_fn_io__printBytes
public zeta_fn_io__printlnBytes

; ABI Zeta : une String est passée sur la pile sous la forme {adresse, longueur}.
; Slice[Byte] possède la même représentation et peut donc partager ce chemin.
zeta_fn_io__printBytes:
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

zeta_fn_io__printlnBytes:
    jmp zeta_fn_io__println

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

zeta_fn_io__printByte:
    movzx eax, byte [rsp+8]
    push rax
    call zeta_fn_io__printInt
    add rsp, 8
    ret

zeta_fn_io__printlnByte:
    movzx eax, byte [rsp+8]
    push rax
    call zeta_fn_io__printByte
    add rsp, 8
    jmp io_append_newline

zeta_fn_io__printBool:
    cmp byte [rsp+8], 0
    je .false
    lea rsi, [io_true]
    mov edx, 4
    jmp io_write_all
.false:
    lea rsi, [io_false]
    mov edx, 5
    jmp io_write_all

zeta_fn_io__printlnBool:
    movzx eax, byte [rsp+8]
    push rax
    call zeta_fn_io__printBool
    add rsp, 8
    jmp io_append_newline

; Format décimal autonome à 7 chiffres significatifs. La notation scientifique
; est utilisée lorsque l'exposant décimal est hors de l'intervalle [-4, 6].
zeta_fn_io__printDouble:
    sub rsp, 96
    mov rax, qword [rsp+104]
    xor r8d, r8d

    ; Le signe est émis avant la classification, ce qui préserve notamment -0.
    bt rax, 63
    jnc .absolute
    mov byte [rsp], '-'
    inc r8d
    btr rax, 63
.absolute:
    mov rdx, 7FF0000000000000h
    mov rcx, rax
    and rcx, rdx
    cmp rcx, rdx
    jne .finite
    mov rdx, 000FFFFFFFFFFFFFh
    test rax, rdx
    jnz .nan
    mov byte [rsp+r8], 'i'
    mov byte [rsp+r8+1], 'n'
    mov byte [rsp+r8+2], 'f'
    add r8d, 3
    jmp .write
.nan:
    ; Un NaN n'a pas de signe textuel dans le format Zeta.
    xor r8d, r8d
    mov byte [rsp], 'n'
    mov byte [rsp+1], 'a'
    mov byte [rsp+2], 'n'
    mov r8d, 3
    jmp .write
.finite:
    mov qword [rsp+80], rax
    movsd xmm0, qword [rsp+80]
    xorpd xmm3, xmm3
    ucomisd xmm0, xmm3
    jne .normalize
    mov byte [rsp+r8], '0'
    inc r8d
    jmp .write

.normalize:
    movsd xmm1, qword [io_double_ten]
    movsd xmm2, qword [io_double_one]
    xor r9d, r9d
.reduce:
    ucomisd xmm0, xmm1
    jb .grow_check
    divsd xmm0, xmm1
    inc r9d
    jmp .reduce
.grow_check:
    ucomisd xmm0, xmm2
    jae .scaled
    mulsd xmm0, xmm1
    dec r9d
    jmp .grow_check
.scaled:
    mulsd xmm0, qword [io_double_million]
    addsd xmm0, qword [io_double_half]
    cvttsd2si r10, xmm0
    cmp r10, 10000000
    jb .digits
    mov r10, 1000000
    inc r9d

    ; Produit exactement sept chiffres significatifs dans le tampon temporaire.
.digits:
    lea rdi, [rsp+71]
    mov rcx, 7
    mov rax, r10
    mov r11, 10
.digit_loop:
    xor edx, edx
    div r11
    add dl, '0'
    dec rdi
    mov byte [rdi], dl
    loop .digit_loop

    ; Supprime les zéros finaux, sans supprimer le premier chiffre.
    mov r10d, 7
.trim:
    cmp r10d, 1
    je .choose_format
    cmp byte [rsp+64+r10-1], '0'
    jne .choose_format
    dec r10d
    jmp .trim

.choose_format:
    cmp r9d, -4
    jl .scientific
    cmp r9d, 6
    jg .scientific
    test r9d, r9d
    js .fixed_fraction

    ; Partie entière puis éventuelle partie fractionnaire.
    xor ecx, ecx
    lea r11d, [r9d+1]
.fixed_integer_digits:
    cmp ecx, r10d
    jae .fixed_integer_zeros
    cmp ecx, r11d
    jae .fixed_dot
    mov al, byte [rsp+64+rcx]
    mov byte [rsp+r8], al
    inc r8d
    inc ecx
    jmp .fixed_integer_digits
.fixed_integer_zeros:
    cmp ecx, r11d
    jae .write
    mov byte [rsp+r8], '0'
    inc r8d
    inc ecx
    jmp .fixed_integer_zeros
.fixed_dot:
    mov byte [rsp+r8], '.'
    inc r8d
.fixed_tail:
    cmp ecx, r10d
    jae .write
    mov al, byte [rsp+64+rcx]
    mov byte [rsp+r8], al
    inc r8d
    inc ecx
    jmp .fixed_tail

.fixed_fraction:
    mov byte [rsp+r8], '0'
    mov byte [rsp+r8+1], '.'
    add r8d, 2
    mov ecx, r9d
    neg ecx
    dec ecx
.leading_zeros:
    test ecx, ecx
    jz .fraction_digits
    mov byte [rsp+r8], '0'
    inc r8d
    dec ecx
    jmp .leading_zeros
.fraction_digits:
    xor ecx, ecx
.fraction_loop:
    cmp ecx, r10d
    jae .write
    mov al, byte [rsp+64+rcx]
    mov byte [rsp+r8], al
    inc r8d
    inc ecx
    jmp .fraction_loop

.scientific:
    mov al, byte [rsp+64]
    mov byte [rsp+r8], al
    inc r8d
    cmp r10d, 1
    je .exponent
    mov byte [rsp+r8], '.'
    inc r8d
    mov ecx, 1
.scientific_tail:
    cmp ecx, r10d
    jae .exponent
    mov al, byte [rsp+64+rcx]
    mov byte [rsp+r8], al
    inc r8d
    inc ecx
    jmp .scientific_tail
.exponent:
    mov byte [rsp+r8], 'e'
    inc r8d
    mov eax, r9d
    test eax, eax
    jns .exponent_positive
    mov byte [rsp+r8], '-'
    inc r8d
    neg eax
    jmp .exponent_digits
.exponent_positive:
    mov byte [rsp+r8], '+'
    inc r8d
.exponent_digits:
    lea rdi, [rsp+63]
    xor ecx, ecx
    mov r11d, 10
.exponent_divide:
    xor edx, edx
    div r11d
    add dl, '0'
    mov byte [rdi], dl
    dec rdi
    inc ecx
    test eax, eax
    jnz .exponent_divide
    inc rdi
.exponent_copy:
    test ecx, ecx
    jz .write
    mov al, byte [rdi]
    mov byte [rsp+r8], al
    inc rdi
    inc r8d
    dec ecx
    jmp .exponent_copy

.write:
    mov rsi, rsp
    mov edx, r8d
    call io_write_all
    add rsp, 96
    ret

zeta_fn_io__printlnDouble:
    push qword [rsp+8]
    call zeta_fn_io__printDouble
    add rsp, 8
    jmp io_append_newline

io_append_newline:
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
io_true: db 'true'
io_false: db 'false'
io_double_one: dq 1.0
io_double_ten: dq 10.0
io_double_half: dq 0.5
io_double_million: dq 1000000.0

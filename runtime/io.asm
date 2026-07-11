format ELF64

section '.text' executable

public zeta_fn_io__print
public zeta_fn_io__println

; ABI Zeta : une String est passée sur la pile sous la forme {adresse, longueur}.
; Ces stubs sont remplacés progressivement par les primitives Linux write(2).
zeta_fn_io__print:
    xor eax, eax
    ret

zeta_fn_io__println:
    xor eax, eax
    ret

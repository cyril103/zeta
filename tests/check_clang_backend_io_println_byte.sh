#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm" "${output}.stdout" "${output}.expected" "${output}.fasm.stdout"
"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
test -f "${output}.ir"
test ! -f "${output}.asm"
grep -Fq 'declare i32 @printf(ptr, ...)' "${output}.ll"
grep -Fq '@zeta.fmt.byte' "${output}.ll"
grep -Fq 'define internal void @zeta_rt_io_write_byte(i8 %value, i1 %newline)' "${output}.ll"
grep -Fq 'zext i8' "${output}.ll"
grep -Fq 'call void @zeta_rt_io_write_byte(i8' "${output}.ll"
if [[ $(grep -Fc 'define internal void @zeta_rt_io_write_byte' "${output}.ll") -ne 1 ]]; then
    echo "expected one zeta_rt_io_write_byte definition" >&2
    exit 1
fi
if [[ $(grep -Fc 'call void @zeta_rt_io_write_byte' "${output}.ll") -ne 2 ]]; then
    echo "expected two zeta_rt_io_write_byte calls" >&2
    exit 1
fi
"${output}" > "${output}.stdout"
printf '7250\n' > "${output}.expected"
cmp -s "${output}.expected" "${output}.stdout"
"${compiler}" "${source}" -o "${output}.fasm" >/dev/null
"${output}.fasm" > "${output}.fasm.stdout"
cmp -s "${output}.fasm.stdout" "${output}.stdout"

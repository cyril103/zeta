#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm" "${output}.stdout" "${output}.fasm.stdout" "${output}.expected"
"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
test -f "${output}.ir"
test ! -f "${output}.asm"
grep -Fq "define internal { ptr, i64 } @zeta_rt_string_from_char(i32 %code)" "${output}.ll"
if [[ $(grep -Fc "define internal { ptr, i64 } @zeta_rt_string_from_char" "${output}.ll") -ne 1 ]]; then
    echo "expected one zeta_rt_string_from_char definition" >&2
    exit 1
fi
if [[ $(grep -Ec '^  %v[0-9]+ = call \{ ptr, i64 \} @zeta_rt_string_from_char' "${output}.ll") -ne 3 ]]; then
    echo "expected three zeta_rt_string_from_char application calls" >&2
    exit 1
fi
grep -Fq "call { ptr, i64 } @zeta_rt_string_from_char(i32 65)" "${output}.ll"
grep -Fq "call { ptr, i64 } @zeta_rt_string_from_char(i32 %" "${output}.ll"
grep -Fq "call { ptr, i64 } @zeta_rt_string_from_char(i32 128640)" "${output}.ll"
grep -Fq "call ptr @malloc(i64" "${output}.ll"
grep -Fq "lshr i32 %code" "${output}.ll"
grep -Fq "trunc i32" "${output}.ll"
"${output}" > "${output}.stdout"
printf 'A / é / 🚀\n' > "${output}.expected"
cmp -s "${output}.expected" "${output}.stdout"
"${compiler}" "${source}" --backend=fasm -o "${output}.fasm"
"${output}.fasm" > "${output}.fasm.stdout"
cmp -s "${output}.fasm.stdout" "${output}.stdout"

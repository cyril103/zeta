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
test ! -e "${output}.asm"
grep -Fq "define internal { ptr, i64 } @zeta_rt_string_from_byte(i8 %value)" "${output}.ll"
grep -Fq "define internal { ptr, i64 } @zeta_rt_string_from_int(i32 %value)" "${output}.ll"
grep -Fq "zext i8 %value to i32" "${output}.ll"
grep -Fq "call { ptr, i64 } @zeta_rt_string_from_int(i32" "${output}.ll"
if [[ $(grep -Fc "define internal { ptr, i64 } @zeta_rt_string_from_byte" "${output}.ll") -ne 1 ]]; then
    echo "expected one zeta_rt_string_from_byte definition" >&2
    exit 1
fi
if [[ $(grep -Fc "call { ptr, i64 } @zeta_rt_string_from_byte" "${output}.ll") -ne 2 ]]; then
    echo "expected two zeta_rt_string_from_byte application calls" >&2
    exit 1
fi
"${output}" > "${output}.stdout"
printf '7 / 250\n' > "${output}.expected"
cmp "${output}.expected" "${output}.stdout"
"${compiler}" "${source}" -o "${output}.fasm"
"${output}.fasm" > "${output}.fasm.stdout"
cmp "${output}.fasm.stdout" "${output}.stdout"

#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm"

"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
test -f "${output}.ir"
test ! -f "${output}.asm"

grep -Fq "define internal i32 @zeta_rt_strings_decode_at_byte(ptr %data, i64 %len, i32 %offset)" "${output}.ll"
grep -Fq "define internal i32 @zeta_rt_strings_next_byte_offset(ptr %data, i64 %len, i32 %offset)" "${output}.ll"
test "$(grep -Fc 'define internal i32 @zeta_rt_strings_decode_at_byte' "${output}.ll")" -eq 1
test "$(grep -Fc 'define internal i32 @zeta_rt_strings_next_byte_offset' "${output}.ll")" -eq 1
test "$(grep -Fc 'call i32 @zeta_rt_strings_decode_at_byte' "${output}.ll")" -eq 5
test "$(grep -Fc 'call i32 @zeta_rt_strings_next_byte_offset' "${output}.ll")" -eq 1
grep -Fq "and i32" "${output}.ll"
grep -Fq "shl i32" "${output}.ll"
grep -Fq "or i32" "${output}.ll"
grep -Fq "icmp uge i32" "${output}.ll"
grep -Fq "select i1" "${output}.ll"

set +e
"${output}"
clang_status=$?
"${compiler}" "${source}" -o "${output}.fasm" >/dev/null
"${output}.fasm"
fasm_status=$?
set -e
if [ "${clang_status}" -ne 154 ]; then
    echo "unexpected clang backend exit code: ${clang_status}" >&2
    exit 1
fi
if [ "${clang_status}" -ne "${fasm_status}" ]; then
    echo "clang backend exit ${clang_status} differs from fasm ${fasm_status}" >&2
    exit 1
fi

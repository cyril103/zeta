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

grep -Fq "define internal { ptr, i64 } @zeta_rt_strings_view(ptr %data, i64 %len, i32 %start, i32 %end)" "${output}.ll"
grep -Fq "define internal i1 @zeta_rt_strings_view_is_valid(ptr %data)" "${output}.ll"
test "$(grep -Fc 'define internal { ptr, i64 } @zeta_rt_strings_view' "${output}.ll")" -eq 1
test "$(grep -Fc 'define internal i1 @zeta_rt_strings_view_is_valid' "${output}.ll")" -eq 1
test "$(grep -Fc 'call { ptr, i64 } @zeta_rt_strings_view' "${output}.ll")" -eq 3
test "$(grep -Fc 'call i1 @zeta_rt_strings_view_is_valid' "${output}.ll")" -eq 3
grep -Fq "getelementptr i8" "${output}.ll"
grep -Fq "select i1" "${output}.ll"
grep -Fq "ptr null" "${output}.ll"
grep -Fq "icmp ne ptr" "${output}.ll"
grep -Fq "extractvalue { ptr, i64 }" "${output}.ll"

set +e
"${output}"
clang_status=$?
"${compiler}" "${source}" -o "${output}.fasm" >/dev/null
"${output}.fasm"
fasm_status=$?
set -e
if [ "${clang_status}" -ne 8 ]; then
    echo "unexpected clang backend exit code: ${clang_status}" >&2
    exit 1
fi
if [ "${clang_status}" -ne "${fasm_status}" ]; then
    echo "clang backend exit ${clang_status} differs from fasm ${fasm_status}" >&2
    exit 1
fi

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
test ! -e "${output}.asm"

grep -q '@str\.[0-9][0-9]* = private unnamed_addr constant { i64, i64, \[0 x i8\] } { i64 -1, i64 0, \[0 x i8\] c"" }' "${output}.ll"
grep -q '@str\.[0-9][0-9]* = private unnamed_addr constant { i64, i64, \[4 x i8\] } { i64 -1, i64 4, \[4 x i8\] c"zeta" }' "${output}.ll"
helper_defs=$(grep -Fc 'define internal i1 @zeta_rt_string_is_empty(ptr %data, i64 %len)' "${output}.ll")
if [[ "${helper_defs}" -ne 1 ]]; then
    echo "expected one @zeta_rt_string_is_empty definition, got ${helper_defs}" >&2
    exit 1
fi
helper_calls=$(grep -Ec '^  %v[0-9]+ = call i1 @zeta_rt_string_is_empty\(ptr ' "${output}.ll")
if [[ "${helper_calls}" -ne 2 ]]; then
    echo "expected two application calls to @zeta_rt_string_is_empty, got ${helper_calls}" >&2
    exit 1
fi

set +e
"${output}"
clang_status=$?
"${compiler}" "${source}" -o "${output}.fasm" >/dev/null
"${output}.fasm"
fasm_status=$?
set -e

if [[ "${clang_status}" -ne 7 ]]; then
    echo "expected clang backend exit 7, got ${clang_status}" >&2
    exit 1
fi
if [[ "${clang_status}" -ne "${fasm_status}" ]]; then
    echo "clang exit ${clang_status} differs from fasm ${fasm_status}" >&2
    exit 1
fi

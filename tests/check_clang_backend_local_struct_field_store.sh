#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm"
rm -rf "${output}.cache" "${output}.modules" "${output}.fasm.cache" "${output}.fasm.modules"
"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
test -f "${output}.ir"
test ! -e "${output}.asm"
grep -Fq "insertvalue { i32, i32 }" "${output}.ll"
grep -Fq "extractvalue { i32, i32 }" "${output}.ll"
grep -Fq "store { i32, i32 }" "${output}.ll"
set +e
"${output}"
clang_status=$?
"${compiler}" "${source}" -o "${output}.fasm" >/tmp/zeta_local_struct_field_store_fasm_compile.out
"${output}.fasm"
fasm_status=$?
set -e
if [[ "${clang_status}" -ne "${fasm_status}" ]]; then
    echo "clang exit ${clang_status}, fasm exit ${fasm_status}" >&2
    exit 1
fi
if [[ "${clang_status}" -ne 42 ]]; then
    echo "expected exit 42, got ${clang_status}" >&2
    exit 1
fi

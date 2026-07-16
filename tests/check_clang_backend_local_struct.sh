#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm"
rm -rf "${output}.modules" "${output}.fasm.modules"
"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
test -f "${output}.ir"
test ! -e "${output}.asm"
grep -Fq 'alloca { i32, i32 }' "${output}.ll"
grep -Fq 'insertvalue { i32, i32 }' "${output}.ll"
grep -Fq 'extractvalue { i32, i32 }' "${output}.ll"
set +e
"${output}"
clang_status=$?
set -e
"${compiler}" "${source}" -o "${output}.fasm" >/tmp/zeta-clang-backend-local-struct-fasm.log
set +e
"${output}.fasm"
fasm_status=$?
set -e
test "${clang_status}" -eq 42
test "${clang_status}" -eq "${fasm_status}"

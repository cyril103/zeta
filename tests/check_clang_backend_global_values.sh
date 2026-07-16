#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source_dir="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm"
"${compiler}" "${source_dir}/main.zeta" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
grep -q '@slot[0-9]* = global i32 0' "${output}.ll"
grep -q '@slot[0-9]* = global i1 0' "${output}.ll"
grep -q 'store i32 7, ptr @slot' "${output}.ll"
grep -q 'load i1, ptr @slot' "${output}.ll"
set +e
"${output}"
clang_status=$?
set -e
test "${clang_status}" -eq 12
"${compiler}" "${source_dir}/main.zeta" -o "${output}.fasm"
set +e
"${output}.fasm"
fasm_status=$?
set -e
test "${fasm_status}" -eq "${clang_status}"

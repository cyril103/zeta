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

grep -Fq "alloca { i1, i8, i32, double, { ptr, i64 } }" "${output}.ll"
grep -Fq "insertvalue { i1, i8, i32, double, { ptr, i64 } }" "${output}.ll"
grep -Fq "extractvalue { i1, i8, i32, double, { ptr, i64 } }" "${output}.ll"
grep -Fq "store { i1, i8, i32, double, { ptr, i64 } }" "${output}.ll"
grep -Fq "icmp eq i32" "${output}.ll"
grep -Fq "icmp eq i8" "${output}.ll"

set +e
"${output}"
clang_status=$?
set -e

"${compiler}" "${source}" --backend=fasm -o "${output}.fasm"
set +e
"${output}.fasm"
fasm_status=$?
set -e

if [[ ${clang_status} -ne 42 ]]; then
    echo "expected clang exit 42, got ${clang_status}" >&2
    exit 1
fi
if [[ ${fasm_status} -ne ${clang_status} ]]; then
    echo "clang/fasm exit mismatch: clang=${clang_status} fasm=${fasm_status}" >&2
    exit 1
fi

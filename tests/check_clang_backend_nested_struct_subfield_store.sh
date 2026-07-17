#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm"
"${compiler}" "${source}" --backend=clang -o "${output}"
cp "${output}.ir" "${output}.clang.ir"
test -x "${output}"
test -f "${output}.ll"
set +e
"${output}"
clang_status=$?
set -e
if [[ "${clang_status}" -ne 42 ]]; then
    echo "expected clang exit 42, got ${clang_status}" >&2
    exit 1
fi
"${compiler}" "${source}" -o "${output}.fasm"
set +e
"${output}.fasm"
fasm_status=$?
set -e
if [[ "${fasm_status}" -ne 42 ]]; then
    echo "expected fasm exit 42, got ${fasm_status}" >&2
    exit 1
fi
if ! diff -u "${output}.fasm.ir" "${output}.clang.ir" >&2; then
    echo "clang/fasm IR differ for nested struct subfield store" >&2
    exit 1
fi
grep -Fq 'insertvalue { { i32, i32 }, { { ptr, i64 }, i32 }, i1 }' "${output}.ll"
grep -Fq 'insertvalue { i32, i32 }' "${output}.ll"
grep -Fq 'store { { i32, i32 }, { { ptr, i64 }, i32 }, i1 }' "${output}.ll"

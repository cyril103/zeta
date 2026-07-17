#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm"
"${compiler}" "${source}" --backend=clang -o "${output}"
cp "${output}.ir" "${output}.clang.ir"
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
clang_ir="$(cat "${output}.clang.ir")"
fasm_ir="$(cat "${output}.fasm.ir")"
if [[ "${clang_ir}" != "${fasm_ir}" ]]; then
    echo "clang/fasm IR differ for branch copy" >&2
    diff -u "${output}.fasm.ir" "${output}.ir" >&2 || true
    exit 1
fi
if ! grep -q 'alloca i32' "${output}.ll"; then
    echo "expected LLVM IR to materialize repeated copy storage" >&2
    exit 1
fi

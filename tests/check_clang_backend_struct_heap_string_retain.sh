#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" \
    "${output}.stdout" "${output}.fasm.stdout"
rm -rf "${output}.cache" "${output}.modules" "${output}.fasm.cache" "${output}.fasm.modules"

"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
test -f "${output}.ir"
test ! -e "${output}.asm"

grep -Fq "call ptr @malloc" "${output}.ll"
grep -Fq "call void @free" "${output}.ll"
grep -Fq "extractvalue" "${output}.ll"
grep -Fq "insertvalue" "${output}.ll"
grep -q "load i64, ptr" "${output}.ll"
grep -q "add i64 .* 1" "${output}.ll"
grep -q "store i64 .*ptr" "${output}.ll"

set +e
"${output}" > "${output}.stdout"
clang_status=$?
set -e

"${compiler}" "${source}" --backend=fasm -o "${output}.fasm"
set +e
"${output}.fasm" > "${output}.fasm.stdout"
fasm_status=$?
set -e

expected=$'heap retain\n'
if [[ $(cat "${output}.stdout")$'\n' != "${expected}" ]]; then
    echo "unexpected clang stdout:" >&2
    cat "${output}.stdout" >&2
    exit 1
fi
if [[ ${clang_status} -ne 0 ]]; then
    echo "expected clang exit 0, got ${clang_status}" >&2
    exit 1
fi
if [[ ${fasm_status} -ne ${clang_status} ]]; then
    echo "clang/fasm exit mismatch: clang=${clang_status} fasm=${fasm_status}" >&2
    exit 1
fi
if ! cmp -s "${output}.stdout" "${output}.fasm.stdout"; then
    echo "clang/fasm stdout mismatch" >&2
    diff -u "${output}.fasm.stdout" "${output}.stdout" >&2 || true
    exit 1
fi

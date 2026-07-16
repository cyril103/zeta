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

grep -Fq "extractvalue { ptr, i64 }" "${output}.ll"
grep -Fq "utf8_decode" "${output}.ll"
grep -Fq "phi i32" "${output}.ll"
grep -Fq "and i32" "${output}.ll"

set +e
"${output}"
clang_status=$?
"${compiler}" "${source}" -o "${output}.fasm" >/dev/null
"${output}.fasm"
fasm_status=$?
set -e
if [ "${clang_status}" -ne 40 ]; then
    echo "unexpected clang backend exit code: ${clang_status}" >&2
    exit 1
fi
if [ "${clang_status}" -ne "${fasm_status}" ]; then
    echo "clang backend exit ${clang_status} differs from fasm ${fasm_status}" >&2
    exit 1
fi

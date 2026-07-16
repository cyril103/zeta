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

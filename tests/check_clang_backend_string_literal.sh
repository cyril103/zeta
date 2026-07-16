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

grep -q '@str\.1 = private unnamed_addr constant \[4 x i8\] c"zeta"' "${output}.ll"
grep -q 'extractvalue { ptr, i64 }' "${output}.ll"

set +e
"${output}"
clang_status=$?
"${compiler}" "${source}" -o "${output}.fasm" >/dev/null
"${output}.fasm"
fasm_status=$?
set -e

if [[ "${clang_status}" -ne 4 ]]; then
    echo "expected clang backend exit 4, got ${clang_status}" >&2
    exit 1
fi
if [[ "${clang_status}" -ne "${fasm_status}" ]]; then
    echo "clang exit ${clang_status} differs from fasm ${fasm_status}" >&2
    exit 1
fi

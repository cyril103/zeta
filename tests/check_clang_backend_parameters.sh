#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm"
"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
test -f "${output}.ir"
test ! -e "${output}.asm"
grep -q 'define i32 @.*__add(i32 %arg0, i32 %arg1)' "${output}.ll"
grep -q 'call i1 @.*__isSmall(i32 %v' "${output}.ll"
set +e
"${output}"
status=$?
set -e
test "${status}" -eq 12

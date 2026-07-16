#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll"
"${compiler}" "${source}" --emit-llvm -o "${output}"
test ! -e "${output}"
test -f "${output}.ll"
grep -q 'define i32 @.*__add(i32 %arg0, i32 %arg1)' "${output}.ll"
grep -q 'define i1 @.*__isSmall(i32 %arg0)' "${output}.ll"
grep -q 'call i32 @.*__add(i32 3, i32 4)' "${output}.ll"
grep -q 'call i1 @.*__isSmall(i32 %v' "${output}.ll"
grep -q 'call i32 @.*__add(i32 10, i32 2)' "${output}.ll"
clang -x ir "${output}.ll" -o "${output}"
set +e
"${output}"
status=$?
set -e
test "${status}" -eq 12

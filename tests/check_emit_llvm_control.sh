#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll"
"${compiler}" "${source}" --emit-llvm -o "${output}"
test ! -e "${output}"
test -f "${output}.ll"
grep -q 'br i1' "${output}.ll"
grep -q 'br label' "${output}.ll"
grep -q '^label[0-9][0-9]*:' "${output}.ll"
grep -q 'alloca i32' "${output}.ll"
grep -q 'store i32' "${output}.ll"
grep -q 'load i32' "${output}.ll"
clang -x ir "${output}.ll" -o "${output}"
set +e
"${output}"
status=$?
set -e
test "${status}" -eq 23

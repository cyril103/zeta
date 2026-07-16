#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll"
"${compiler}" "${source}" --emit-llvm -o "${output}"
test ! -e "${output}"
test -f "${output}.ll"
grep -q 'target triple = "x86_64-pc-linux-gnu"' "${output}.ll"
grep -q 'define i32 @main()' "${output}.ll"
grep -q 'ret i32 42' "${output}.ll"
clang -x ir "${output}.ll" -o "${output}"
set +e
"${output}"
status=$?
set -e
test "${status}" -eq 42

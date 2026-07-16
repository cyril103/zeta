#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll"
"${compiler}" "${source}" --emit-llvm -o "${output}"
test ! -e "${output}"
test -f "${output}.ll"
grep -q 'mul nsw i32' "${output}.ll"
grep -q 'sdiv i32' "${output}.ll"
grep -q 'sub nsw i32' "${output}.ll"
grep -q 'add nsw i32' "${output}.ll"
grep -q 'icmp slt i32' "${output}.ll"
grep -q 'icmp eq i32' "${output}.ll"
clang -x ir "${output}.ll" -o "${output}"
set +e
"${output}"
status=$?
set -e
test "${status}" -eq 46

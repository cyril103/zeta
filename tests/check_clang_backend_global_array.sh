#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.diag"

"${compiler}" "${source}" --backend=clang -o "${output}" >"${output}.diag" 2>&1

test -x "${output}"
test -f "${output}.ll"
grep -Eq '@slot[0-9]+ = global \[3 x i32\] zeroinitializer' "${output}.ll"
grep -Eq 'store \[3 x i32\]' "${output}.ll"
grep -Eq 'load \[3 x i32\], ptr @slot[0-9]+' "${output}.ll"
grep -Eq 'getelementptr inbounds \[3 x i32\], ptr %v[0-9]+\.array, i32 0, i32 2' "${output}.ll"
grep -Eq 'load i32, ptr' "${output}.ll"
set +e
"${output}"
status=$?
set -e
test "${status}" -eq 40

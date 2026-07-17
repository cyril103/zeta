#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.diag"

"${compiler}" "${source}" --backend=clang -o "${output}" >"${output}.diag" 2>&1

test -x "${output}"
test -f "${output}.ll"
grep -q '@slot[0-9][0-9]* = global { i32, i32 } zeroinitializer' "${output}.ll"
grep -q 'store { i32, i32 }' "${output}.ll"
grep -q 'load { i32, i32 }, ptr @slot' "${output}.ll"
set +e
"${output}"
status=$?
set -e
test "${status}" -eq 3

#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.diag" "${output}.stdout"

"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
grep -q '@slot[0-9]* = global i32 0' "${output}.ll"
grep -q 'store i32 65, ptr @slot' "${output}.ll"
grep -q 'load i32, ptr @slot' "${output}.ll"
set +e
"${output}" >"${output}.stdout"
status=$?
set -e
grep -qx 'A' "${output}.stdout"
test "${status}" -eq 65

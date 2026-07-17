#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.diag" "${output}.stdout"

"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
grep -q '@slot[0-9]* = global i8 0' "${output}.ll"
grep -q 'store i8 250, ptr @slot' "${output}.ll"
grep -q 'load i8, ptr @slot' "${output}.ll"
set +e
"${output}" >"${output}.stdout"
status=$?
set -e
grep -qx '250' "${output}.stdout"
test "${status}" -eq 250

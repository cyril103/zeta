#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm"
"${compiler}" "${source}" -o "${output}"
test -x "${output}"
test -f "${output}.asm"
test -f "${output}.ir"
test ! -e "${output}.ll"
set +e
"${output}"
status=$?
set -e
test "${status}" -eq 12

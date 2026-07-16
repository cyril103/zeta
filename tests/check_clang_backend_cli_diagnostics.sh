#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -rf "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.diag"

expect_failure() {
    local expected="$1"
    shift
    set +e
    "$@" >"${output}.diag" 2>&1
    local status=$?
    set -e
    test "${status}" -ne 0
    grep -q -- "${expected}" "${output}.diag"
}

expect_failure "backend inconnu 'wat'" \
    "${compiler}" "${source}" --backend=wat -o "${output}"
expect_failure "--emit-llvm requiert le backend clang" \
    "${compiler}" "${source}" --emit-llvm --backend=fasm -o "${output}"
expect_failure "--backend=clang et --emit-llvm sont réservés aux exécutables" \
    "${compiler}" --build-library "${source}" --backend=clang -o "${output}"

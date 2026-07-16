#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
global_source="$2"
local_source="$3"
output="$4"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.diag"

expect_failure() {
    local source="$1"
    local expected="$2"
    rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.diag"
    if "${compiler}" "${source}" --backend=clang -o "${output}" >"${output}.diag" 2>&1; then
        cat "${output}.diag"
        exit 1
    fi
    grep -q "${expected}" "${output}.diag"
    test ! -e "${output}.ll"
    test ! -e "${output}"
}

expect_failure "${global_source}" 'backend LLVM: globale non scalaire non supportée greeting: String'
expect_failure "${local_source}" 'backend LLVM: slot local non scalaire non supporté greeting: String'

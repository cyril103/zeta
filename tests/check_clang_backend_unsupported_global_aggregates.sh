#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
struct_source="$2"
vec_source="$3"
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
    grep -Fq "${expected}" "${output}.diag"
    test ! -e "${output}.ll"
    test ! -e "${output}"
}

expect_failure "${struct_source}" 'backend LLVM: agrégat global non supporté pair: Pair'
expect_failure "${vec_source}" 'backend LLVM: agrégat global non supporté values: Vec[Int]'

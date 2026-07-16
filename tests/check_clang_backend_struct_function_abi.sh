#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm"
rm -rf "${output}.cache" "${output}.modules" "${output}.fasm.cache" "${output}.fasm.modules"
"${compiler}" "${source}" -o "${output}" --backend=clang
if [[ ! -x "${output}" ]]; then
    echo "expected clang backend executable" >&2
    exit 1
fi
if [[ ! -f "${output}.ll" || ! -f "${output}.ir" ]]; then
    echo "expected clang backend .ll and .ir artifacts" >&2
    exit 1
fi
if [[ -e "${output}.asm" ]]; then
    echo "clang backend should not produce .asm" >&2
    exit 1
fi
grep -Fq "define { i32, i32 } @" "${output}.ll"
grep -Fq "__makePair(i32 %arg0)" "${output}.ll"
grep -Fq "define i32 @" "${output}.ll"
grep -Fq "__sumPair({ i32, i32 } %arg0)" "${output}.ll"
grep -Fq "__shiftPair({ i32, i32 } %arg0, i32 %arg1)" "${output}.ll"
grep -Fq "call { i32, i32 } @" "${output}.ll"
grep -Fq "__makePair(i32 0)" "${output}.ll"
grep -Fq "__shiftPair({ i32, i32 }" "${output}.ll"
set +e
"${output}"
clang_status=$?
set -e
"${compiler}" "${source}" -o "${output}.fasm"
set +e
"${output}.fasm"
fasm_status=$?
set -e
if [[ "${clang_status}" -ne 42 ]]; then
    echo "expected clang exit 42, got ${clang_status}" >&2
    exit 1
fi
if [[ "${clang_status}" -ne "${fasm_status}" ]]; then
    echo "clang exit ${clang_status} differs from fasm ${fasm_status}" >&2
    exit 1
fi

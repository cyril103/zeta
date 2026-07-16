#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm" "${output}.out" "${output}.fasm.out" "${output}.expected"

"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
test -f "${output}.ir"
test ! -f "${output}.asm"

grep -Fq "declare i32 @printf(ptr, ...)" "${output}.ll"
grep -Fq "@zeta.fmt.int" "${output}.ll"
grep -Fq "@zeta.fmt.int.nl" "${output}.ll"
grep -Fq "call i32 (ptr, ...) @printf" "${output}.ll"

"${output}" > "${output}.out"
"${compiler}" "${source}" -o "${output}.fasm" >/dev/null
"${output}.fasm" > "${output}.fasm.out"

printf '402\n' > "${output}.expected"
if ! cmp -s "${output}.expected" "${output}.out"; then
    echo "unexpected clang stdout" >&2
    diff -u "${output}.expected" "${output}.out" >&2 || true
    exit 1
fi
if ! cmp -s "${output}.fasm.out" "${output}.out"; then
    echo "clang backend stdout differs from fasm" >&2
    diff -u "${output}.fasm.out" "${output}.out" >&2 || true
    exit 1
fi

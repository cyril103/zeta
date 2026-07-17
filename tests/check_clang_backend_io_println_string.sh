#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm" "${output}.out" "${output}.fasm.out"

"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
test -f "${output}.ir"
test ! -f "${output}.asm"

grep -Fq "declare i64 @write(i32, ptr, i64)" "${output}.ll"
grep -Fq "define internal void @zeta_rt_io_write_string(ptr %data, i64 %len, i1 %newline)" "${output}.ll"
grep -Fq "call void @zeta_rt_io_write_string(ptr " "${output}.ll"
grep -Fq "i1 false)" "${output}.ll"
grep -Fq "i1 true)" "${output}.ll"
grep -Fq "@zeta.newline" "${output}.ll"
if [[ $(grep -Fc "define internal void @zeta_rt_io_write_string" "${output}.ll") -ne 1 ]]; then
    echo "expected one zeta_rt_io_write_string definition" >&2
    exit 1
fi
if [[ $(grep -Fc "call void @zeta_rt_io_write_string" "${output}.ll") -ne 2 ]]; then
    echo "expected two zeta_rt_io_write_string calls" >&2
    exit 1
fi

"${output}" > "${output}.out"
"${compiler}" "${source}" -o "${output}.fasm" >/dev/null
"${output}.fasm" > "${output}.fasm.out"

printf 'zeta 🚀\n' > "${output}.expected"
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

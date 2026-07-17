#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm" "${output}.stdout" "${output}.fasm.stdout" "${output}.expected"
"${compiler}" "${source}" --backend=clang -o "${output}"
test -x "${output}"
test -f "${output}.ll"
test -f "${output}.ir"
test ! -e "${output}.asm"
grep -Fq "declare i64 @write(i32, ptr, i64)" "${output}.ll"
grep -Fq "@zeta.bool.true" "${output}.ll"
grep -Fq "@zeta.bool.false" "${output}.ll"
grep -Fq "define internal void @zeta_rt_io_write_bool(i1 %value, i1 %newline)" "${output}.ll"
grep -Fq "call void @zeta_rt_io_write_bool(i1 1, i1 false)" "${output}.ll"
grep -Fq "call void @zeta_rt_io_write_bool(i1 0, i1 true)" "${output}.ll"
grep -Fq "call i64 @write(i32 1" "${output}.ll"
if [[ $(grep -Fc "define internal void @zeta_rt_io_write_bool" "${output}.ll") -ne 1 ]]; then
    echo "expected one zeta_rt_io_write_bool definition" >&2
    exit 1
fi
if [[ $(grep -Fc "call void @zeta_rt_io_write_bool" "${output}.ll") -ne 2 ]]; then
    echo "expected two zeta_rt_io_write_bool calls" >&2
    exit 1
fi
"${output}" > "${output}.stdout"
printf 'truefalse\n' > "${output}.expected"
cmp "${output}.expected" "${output}.stdout"
"${compiler}" "${source}" -o "${output}.fasm"
"${output}.fasm" > "${output}.fasm.stdout"
cmp "${output}.fasm.stdout" "${output}.stdout"

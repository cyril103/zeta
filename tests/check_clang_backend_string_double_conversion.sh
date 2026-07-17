#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.asm" "${output}.fasm" "${output}.fasm.ir" "${output}.fasm.asm" "${output}.out" "${output}.fasm.out"
"${compiler}" --backend=clang "${source}" -o "${output}"
if [[ -e "${output}.asm" || -e "${output}.fasm" || -e "${output}.fasm.ir" || -e "${output}.fasm.asm" ]]; then
    echo "clang backend unexpectedly produced FASM artifacts" >&2
    exit 1
fi
grep -Fq "define internal { ptr, i64 } @zeta_rt_string_from_double(double %value)" "${output}.ll"
if [[ $(grep -Fc "define internal { ptr, i64 } @zeta_rt_string_from_double" "${output}.ll") -ne 1 ]]; then
    echo "expected one zeta_rt_string_from_double definition" >&2
    exit 1
fi
if [[ $(grep -Ec '^  %v[0-9]+ = call \{ ptr, i64 \} @zeta_rt_string_from_double' "${output}.ll") -ne 3 ]]; then
    echo "expected three zeta_rt_string_from_double application calls" >&2
    exit 1
fi
grep -Fq "call i32 (ptr, i64, ptr, ...) @snprintf" "${output}.ll"
grep -Fq "call ptr @malloc" "${output}.ll"
"${output}" > "${output}.out"
printf '18.5 / -0.25 / 0\n' > "${output}.expected"
diff -u "${output}.expected" "${output}.out"
"${compiler}" --backend=fasm "${source}" -o "${output}.fasm"
"${output}.fasm" > "${output}.fasm.out"
diff -u "${output}.fasm.out" "${output}.out"

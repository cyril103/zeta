#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
work="$2"
rm -rf "${work}"
mkdir -p "${work}/src" "${work}/lib"
cat >"${work}/src/mathlib.zeta" <<'ZETA'
def add(a: Int, b: Int): Int = a + b
ZETA
"${compiler}" --build-library "${work}/src/mathlib.zeta" --backend=clang -o "${work}/lib" >"${work}/build.log"
test -f "${work}/lib/mathlib.zti"
test -f "${work}/lib/mathlib.o"
test -f "${work}/lib/mathlib.ll"
if grep -q 'define i32 @main()' "${work}/lib/mathlib.ll"; then
    echo "library LLVM IR must not define executable main" >&2
    exit 1
fi
grep -q 'define i32 @mathlib__add(i32 %arg0, i32 %arg1)' "${work}/lib/mathlib.ll"
if [ -e "${work}/lib/mathlib.asm" ]; then
    echo "clang library build must not publish FASM assembly" >&2
    exit 1
fi
nm -g "${work}/lib/mathlib.o" | grep -q 'mathlib__add'

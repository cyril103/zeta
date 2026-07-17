#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
work="$2"
rm -rf "${work}"
mkdir -p "${work}/source" "${work}/dist" "${work}/cache" "${work}/consumer"

cat >"${work}/source/settings.zeta" <<'ZETA'
pub struct Pair {
    left: Int
    right: Int
}

pub val pair: Pair = Pair { left: 3, right: 4 }
ZETA

"${compiler}" --build-library "${work}/source/settings.zeta" \
    --backend=clang \
    -o "${work}/dist"

test -f "${work}/dist/settings.o"
test -f "${work}/dist/settings.ll"
grep -Fq '@settings__pair = global { i32, i32 } { i32 3, i32 4 }' "${work}/dist/settings.ll"
if grep -Fq 'store { i32, i32 }' "${work}/dist/settings.ll"; then
    echo "precompiled struct global must use a static initializer, not a top-level store" >&2
    exit 1
fi

"${compiler}" --install-library "${work}/dist/settings.zti" --library-cache "${work}/cache" >/dev/null

cat >"${work}/consumer/app.zeta" <<'ZETA'
import settings

def main(): Int = {
    val pair = settings.pair
    pair.left + pair.right
}
ZETA

"${compiler}" "${work}/consumer/app.zeta" \
    --backend=clang \
    --library-cache "${work}/cache" \
    -o "${work}/consumer/app"

grep -Fq '@settings__pair = external global { i32, i32 }' "${work}/consumer/app.ll"
grep -Fq 'load { i32, i32 }, ptr @settings__pair' "${work}/consumer/app.ll"
set +e
"${work}/consumer/app"
status=$?
set -e
if [[ "${status}" -ne 7 ]]; then
    echo "expected app to return 7, got ${status}" >&2
    exit 1
fi

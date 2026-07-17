#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
work="$2"
rm -rf "${work}"
mkdir -p "${work}/source" "${work}/dist" "${work}/cache" "${work}/consumer"

cat >"${work}/source/settings.zeta" <<'ZETA'
pub val greeting: String = "zeta"
ZETA

"${compiler}" --build-library "${work}/source/settings.zeta" \
    --backend=clang \
    -o "${work}/dist"

test -f "${work}/dist/settings.o"
test -f "${work}/dist/settings.ll"
grep -Fq '@settings__greeting = global { ptr, i64 }' "${work}/dist/settings.ll"
grep -Fq 'private unnamed_addr constant { i64, i64, [4 x i8] }' "${work}/dist/settings.ll"
grep -Fq 'c"zeta"' "${work}/dist/settings.ll"

"${compiler}" --install-library "${work}/dist/settings.zti" --library-cache "${work}/cache" >/dev/null

cat >"${work}/consumer/app.zeta" <<'ZETA'
import settings

def main(): Int = {
    val greeting = settings.greeting
    greeting.lengthBytes
}
ZETA

"${compiler}" "${work}/consumer/app.zeta" \
    --backend=clang \
    --library-cache "${work}/cache" \
    -o "${work}/consumer/app"

grep -Fq '@settings__greeting = external global { ptr, i64 }' "${work}/consumer/app.ll"
grep -Fq 'load { ptr, i64 }, ptr @settings__greeting' "${work}/consumer/app.ll"
set +e
"${work}/consumer/app"
status=$?
set -e
if [[ "${status}" -ne 4 ]]; then
    echo "expected app to return string length 4, got ${status}" >&2
    exit 1
fi

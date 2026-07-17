#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
work="$2"
rm -rf "${work}"
mkdir -p "${work}/source" "${work}/dist" "${work}/cache" "${work}/consumer"

cat >"${work}/source/settings.zeta" <<'ZETA'
pub val answer: Int = 42
ZETA

"${compiler}" --build-library "${work}/source/settings.zeta" --backend=clang -o "${work}/dist" >/dev/null
"${compiler}" --install-library "${work}/dist/settings.zti" --library-cache "${work}/cache" >/dev/null

cat >"${work}/consumer/main.zeta" <<'ZETA'
import settings

def main(): Int = settings.answer
ZETA

"${compiler}" "${work}/consumer/main.zeta" --backend=clang \
    -o "${work}/consumer/app" --library-cache "${work}/cache" >/dev/null

set +e
"${work}/consumer/app"
status=$?
set -e
test "${status}" -eq 42

test -f "${work}/consumer/app.ll"
test -d "${work}/consumer/app.modules"
test -f "${work}/consumer/app.modules/settings.o"
grep -Eq '^(declare|@settings__answer)' "${work}/consumer/app.ll"

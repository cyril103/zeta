#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
work="$2"
rm -rf "${work}"
mkdir -p "${work}/source" "${work}/dist" "${work}/cache" "${work}/consumer"

cat >"${work}/source/base.zeta" <<'ZETA'
pub def value(): Int = 40
ZETA
cat >"${work}/source/service.zeta" <<'ZETA'
import base
pub def answer(): Int = base.value() + 2
ZETA

"${compiler}" --build-library "${work}/source/base.zeta" --backend=clang -o "${work}/dist" >/dev/null
"${compiler}" --build-library "${work}/source/service.zeta" --backend=clang -o "${work}/dist" >/dev/null
"${compiler}" --install-library "${work}/dist/base.zti" --library-cache "${work}/cache" >/dev/null
"${compiler}" --install-library "${work}/dist/service.zti" --library-cache "${work}/cache" >/dev/null

cat >"${work}/consumer/main.zeta" <<'ZETA'
import service

def main(): Int = service.answer() - 42
ZETA

"${compiler}" "${work}/consumer/main.zeta" --backend=clang \
    -o "${work}/consumer/app" --library-cache "${work}/cache" >/dev/null
"${work}/consumer/app"

test -f "${work}/consumer/app.ll"
test -d "${work}/consumer/app.modules"
test -f "${work}/consumer/app.modules/base.o"
test -f "${work}/consumer/app.modules/service.o"

#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
work="$2"
rm -rf "${work}"
mkdir -p "${work}/stdlib" "${work}/sources"
cat >"${work}/stdlib/base.zeta" <<'ZETA'
pub def value(): Int = 41
ZETA
cat >"${work}/app.zeta" <<'ZETA'
import base

def main(): Int = base.value() + 1
ZETA

"${compiler}" --build-stdlib --backend=clang --stdlib "${work}/stdlib" >/dev/null

test -f "${work}/stdlib/precompiled/manifest"
test -f "${work}/stdlib/precompiled/base.zti"
test -f "${work}/stdlib/precompiled/base.o"
test -f "${work}/stdlib/precompiled/base.ll"
test ! -e "${work}/stdlib/precompiled/.stdlib-build.modules"

mv "${work}/stdlib/base.zeta" "${work}/sources/base.zeta"
"${compiler}" "${work}/app.zeta" --backend=clang --stdlib "${work}/stdlib" -o "${work}/app" >/dev/null
set +e
"${work}/app"
status=$?
set -e
if [ "${status}" -ne 42 ]; then
    echo "unexpected exit status ${status}" >&2
    exit 1
fi

grep -q 'declare i32 @base__value()' "${work}/app.ll"
test -f "${work}/app.modules/base.o"

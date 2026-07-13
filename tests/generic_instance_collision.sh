#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$fixtures"/*.zeta "$work/"

"$compiler" "$work/main.zeta" -o "$work/app" >/dev/null
"$work/app"

count=$(nm --defined-only "$work/app.modules"/*.o |
    grep -c ' zeta_fn_api__identity__Int__g')
test "$count" -eq 1

#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$fixtures"/*.zeta "$work"/

"$compiler" "$work/main.zeta" -o "$work/app" >/dev/null
maths_before=$(stat -c %Y "$work/app.cache/maths.o")
service_before=$(stat -c %Y "$work/app.cache/service.o")
textes_before=$(stat -c %Y "$work/app.cache/textes.o")
main_before=$(stat -c %Y "$work/app.cache/main.o")

sleep 1
sed -i 's/99/100/' "$work/maths.zeta"
"$compiler" "$work/main.zeta" -o "$work/app" >/dev/null

test "$(stat -c %Y "$work/app.cache/maths.o")" -gt "$maths_before"
test "$(stat -c %Y "$work/app.cache/service.o")" = "$service_before"
test "$(stat -c %Y "$work/app.cache/textes.o")" = "$textes_before"
test "$(stat -c %Y "$work/app.cache/main.o")" = "$main_before"
"$work/app"

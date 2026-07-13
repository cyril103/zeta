#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$fixtures"/*.zeta "$work"/

"$compiler" "$work/main.zeta" -o "$work/app" >/dev/null
api_before=$(stat -c %Y "$work/app.cache/api.o")
main_before=$(stat -c %Y "$work/app.cache/main.o")
fingerprint_before=$(sed -n 's/^fingerprint //p' "$work/app.modules/api.zti")

sleep 1
sed -i '1i// commentaire sans effet sémantique' "$work/api.zeta"
"$compiler" "$work/main.zeta" -o "$work/app" >/dev/null

test "$(stat -c %Y "$work/app.cache/api.o")" -gt "$api_before"
test "$(stat -c %Y "$work/app.cache/main.o")" = "$main_before"
test "$(sed -n 's/^fingerprint //p' "$work/app.modules/api.zti")" = "$fingerprint_before"
"$work/app"

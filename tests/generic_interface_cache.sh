#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$fixtures"/*.zeta "$work"/

"$compiler" "$work/main.zeta" -o "$work/app" >/dev/null
grep -q '^generic_tokens 1 ' "$work/app.modules/api.zti"
if grep -q '^generic_source ' "$work/app.modules/api.zti"; then
    exit 1
fi
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

mkdir "$work/consumer"
cp "$work/main.zeta" "$work/consumer/main.zeta"
cp "$work/app.modules/api.zti" "$work/consumer/api.zti"
cp "$work/app.modules/api.o" "$work/consumer/api.o"
"$compiler" "$work/consumer/main.zeta" -o "$work/consumer/app" >/dev/null
"$work/consumer/app"

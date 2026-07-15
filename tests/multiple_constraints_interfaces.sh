#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$compiler" "$fixtures/main.zeta" -o "$work/published" >/dev/null
"$work/published"

interface="$work/published.modules/api.zti"
grep -q '^export "same" .* "T" "Copy+Equatable"$' "$interface"
grep -q '^generic_tokens 4 ' "$interface"

mkdir "$work/consumer"
cp "$fixtures/main.zeta" "$work/consumer/main.zeta"
cp "$interface" "$work/consumer/api.zti"
cp "$work/published.modules/api.o" "$work/consumer/api.o"

"$compiler" "$work/consumer/main.zeta" -o "$work/consumer/app" >/dev/null
"$work/consumer/app"

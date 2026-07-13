#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$compiler" "$fixtures/main.zeta" -o "$work/published" >/dev/null

interface="$work/published.modules/types.zti"
grep -q '^structure "Point"' "$interface"
grep -q '^structure "Pair"' "$interface"
grep -q 'export "origin".*"U5:Point\[0:\]"' "$interface"

mkdir "$work/consumer"
cp "$fixtures/main.zeta" "$work/consumer/main.zeta"
cp "$interface" "$work/consumer/types.zti"
cp "$work/published.modules/types.o" "$work/consumer/types.o"

"$compiler" "$work/consumer/main.zeta" -o "$work/consumer/app" >/dev/null
"$work/consumer/app"

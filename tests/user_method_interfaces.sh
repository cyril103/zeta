#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$compiler" "$fixtures/main.zeta" -o "$work/published" >/dev/null

interface="$work/published.modules/counters.zti"
grep -q '^export "Counter.read"' "$interface"
grep -q '^export "Counter.increment"' "$interface"

mkdir "$work/consumer"
cp "$fixtures/main.zeta" "$work/consumer/main.zeta"
cp "$interface" "$work/consumer/counters.zti"
cp "$work/published.modules/counters.o" "$work/consumer/counters.o"

"$compiler" "$work/consumer/main.zeta" -o "$work/consumer/app" >/dev/null
"$work/consumer/app"

#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$compiler" "$fixtures/main.zeta" -o "$work/published" >/dev/null

interface="$work/published.modules/types.zti"
grep -q '^enumeration "State"' "$interface"
grep -q '^enumeration "Outcome"' "$interface"
grep -q '^variant "Failed"' "$interface"
grep -q 'export "ready".*"EState\[0:\]"' "$interface"
grep -q 'export "zeroOutcome".*"EOutcome\[1:I\]"' "$interface"

mkdir "$work/consumer"
cp "$fixtures/main.zeta" "$work/consumer/main.zeta"
cp "$interface" "$work/consumer/types.zti"
cp "$work/published.modules/types.o" "$work/consumer/types.o"

"$compiler" "$work/consumer/main.zeta" -o "$work/consumer/app" >/dev/null
"$work/consumer/app"

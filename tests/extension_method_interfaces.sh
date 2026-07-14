#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$compiler" "$fixtures/main.zeta" -o "$work/published" >/dev/null
"$work/published"

interface="$work/published.modules/vec_extensions.zti"
grep -q '^export "Vec.appendTwice" 2 1 0 1 ' "$interface"
grep -q '^export "Vec.count" 2 1 0 1 ' "$interface"
grep -q '^generic_tokens 2 ' "$interface"

mkdir "$work/consumer"
cp "$fixtures/main.zeta" "$work/consumer/main.zeta"
cp "$interface" "$work/consumer/vec_extensions.zti"
cp "$work/published.modules/vec_extensions.o" "$work/consumer/vec_extensions.o"

"$compiler" "$work/consumer/main.zeta" -o "$work/consumer/app" >/dev/null
"$work/consumer/app"

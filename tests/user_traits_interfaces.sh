#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$compiler" "$fixtures/main.zeta" -o "$work/published" >/dev/null
"$work/published"

interface="$work/published.modules/capabilities.zti"
grep -q '^trait "capabilities.Answer"$' "$interface"
grep -q '^export "keep" .* "T" "Copy+capabilities.Answer"$' "$interface"
grep -q '^generic_tokens 4 ' "$interface"

models_interface="$work/published.modules/models.zti"
grep -q '^implementation "capabilities.Answer" "U5:Token\[0:\]"$' "$models_interface"

if "$compiler" "$fixtures/orphan.zeta" -o "$work/orphan" >"$work/orphan.log" 2>&1; then
    echo "une implémentation orpheline a été acceptée" >&2
    exit 1
fi
grep -q 'implémentation orpheline' "$work/orphan.log"

mkdir "$work/consumer"
cp "$fixtures/main.zeta" "$work/consumer/main.zeta"
cp "$interface" "$work/consumer/capabilities.zti"
cp "$work/published.modules/capabilities.o" "$work/consumer/capabilities.o"
cp "$models_interface" "$work/consumer/models.zti"
cp "$work/published.modules/models.o" "$work/consumer/models.o"

"$compiler" "$work/consumer/main.zeta" -o "$work/consumer/app" >/dev/null
"$work/consumer/app"

# Une interface forgée ne peut pas contourner la règle orpheline.
sed -i 's/^implementation "capabilities.Answer" .*/implementation "capabilities.Answer" "I"/' \
    "$work/consumer/models.zti"
if "$compiler" "$work/consumer/main.zeta" -o "$work/consumer/forged" \
    >"$work/consumer/forged.log" 2>&1; then
    echo "une implémentation orpheline forgée a été acceptée" >&2
    exit 1
fi
grep -q '\[ZTI400\].*implémentation orpheline' "$work/consumer/forged.log"

#!/usr/bin/env bash
set -euo pipefail

compiler=$1
module_fixtures=$2
generic_root=$3
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Dépendances ordinaires et transitives, sans aucun de leurs fichiers source.
"$compiler" "$module_fixtures/main.zeta" -o "$work/published" >/dev/null
mkdir "$work/ordinary"
cp "$module_fixtures/main.zeta" "$work/ordinary/main.zeta"
for module in maths textes service; do
    cp "$work/published.modules/$module.zti" "$work/ordinary/$module.zti"
    cp "$work/published.modules/$module.o" "$work/ordinary/$module.o"
done
"$compiler" "$work/ordinary/main.zeta" -o "$work/ordinary/app" >/dev/null
"$work/ordinary/app"

# Corps génériques monomorphisés depuis collections.zti, sans collections.zeta.
"$compiler" "$generic_root" -o "$work/generic-published" >/dev/null
mkdir "$work/generic"
cp "$generic_root" "$work/generic/collections_module.zeta"
cp "$work/generic-published.modules/collections.zti" "$work/generic/collections.zti"
cp "$work/generic-published.modules/collections.o" "$work/generic/collections.o"
"$compiler" "$work/generic/collections_module.zeta" -o "$work/generic/app" >/dev/null
"$work/generic/app"
grep -q '^generic_tokens 4 ' "$work/generic/collections.zti"
if grep -q '^generic_source ' "$work/generic/collections.zti"; then
    exit 1
fi

# Une version inconnue ou une suite sans token End doit être rejetée.
mkdir "$work/invalid-generic-version"
cp "$generic_root" "$work/invalid-generic-version/collections_module.zeta"
cp "$work/generic-published.modules/collections.o" \
   "$work/invalid-generic-version/collections.o"
printf 'ZTI 12\nmodule "collections"\nfingerprint invalid\ngeneric_tokens 999 1\ntoken 0 1 1 ""\nend\n' \
    > "$work/invalid-generic-version/collections.zti"
if "$compiler" "$work/invalid-generic-version/collections_module.zeta" \
    -o "$work/invalid-generic-version/app" >"$work/generic-version-error" 2>&1; then
    exit 1
fi
grep -q 'représentation générique .zti invalide' "$work/generic-version-error"

mkdir "$work/invalid-generic-end"
cp "$generic_root" "$work/invalid-generic-end/collections_module.zeta"
cp "$work/generic-published.modules/collections.o" \
   "$work/invalid-generic-end/collections.o"
printf 'ZTI 12\nmodule "collections"\nfingerprint invalid\ngeneric_tokens 4 1\ntoken 0 1 1 ""\nend\n' \
    > "$work/invalid-generic-end/collections.zti"
if "$compiler" "$work/invalid-generic-end/collections_module.zeta" \
    -o "$work/invalid-generic-end/app" >"$work/generic-end-error" 2>&1; then
    exit 1
fi
grep -q 'fin des tokens génériques .zti absente' "$work/generic-end-error"

# Une version inconnue doit être diagnostiquée avant l'édition de liens.
mkdir "$work/invalid"
cp "$module_fixtures/main.zeta" "$work/invalid/main.zeta"
printf 'ZTI 999\n' > "$work/invalid/service.zti"
cp "$work/published.modules/service.o" "$work/invalid/service.o"
if "$compiler" "$work/invalid/main.zeta" -o "$work/invalid/app" >"$work/error" 2>&1; then
    exit 1
fi
grep -q 'format .zti inconnu ou incompatible' "$work/error"

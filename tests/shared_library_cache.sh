#!/usr/bin/env bash
set -euo pipefail

compiler=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/source" "$work/dist" "$work/cache" "$work/consumer"

printf 'pub def value(): Int = 40\n' > "$work/source/base.zeta"
printf 'import base\npub def answer(): Int = base.value() + 2\n' \
    > "$work/source/service.zeta"
"$compiler" --build-library "$work/source/base.zeta" -o "$work/dist" >/dev/null
"$compiler" --build-library "$work/source/service.zeta" -o "$work/dist" >/dev/null

# Une bibliothèque s'installe après les dépendances qu'elle déclare.
if "$compiler" --install-library "$work/dist/service.zti" \
    --library-cache "$work/cache" >"$work/missing-dependency" 2>&1; then
    exit 1
fi
grep -q '\[LIB002\]' "$work/missing-dependency"
"$compiler" --install-library "$work/dist/base.zti" \
    --library-cache "$work/cache" >/dev/null
"$compiler" --install-library "$work/dist/service.zti" \
    --library-cache "$work/cache" >/dev/null

cache_abi=$(find "$work/cache" -mindepth 1 -maxdepth 1 -type d -name 'abi-*')
test -f "$cache_abi/base.zti"
test -f "$cache_abi/base.o"
test -f "$cache_abi/service.zti"
test -f "$cache_abi/service.o"

# Le projet consommateur ne possède aucune paire locale ni aucun source producteur.
printf 'import service\ndef main(): Int = service.answer() - 42\n' \
    > "$work/consumer/main.zeta"
"$compiler" "$work/consumer/main.zeta" -o "$work/consumer/app" \
    --library-cache "$work/cache" >/dev/null
"$work/consumer/app"

# La variable d'environnement sélectionne le même cache sans option CLI.
ZETA_LIBRARY_CACHE="$work/cache" "$compiler" "$work/consumer/main.zeta" \
    -o "$work/consumer/app-env" >/dev/null
"$work/consumer/app-env"

# Un module source local garde priorité sur le module homonyme installé.
mkdir "$work/local-priority"
printf 'pub def value(): Int = 0\n' > "$work/local-priority/base.zeta"
printf 'import base\ndef main(): Int = base.value()\n' \
    > "$work/local-priority/main.zeta"
"$compiler" "$work/local-priority/main.zeta" -o "$work/local-priority/app" \
    --library-cache "$work/cache" >/dev/null
"$work/local-priority/app"

# Une même empreinte est réinstallable ; une nouvelle interface exige --force.
"$compiler" --install-library "$work/dist/base.zti" \
    --library-cache "$work/cache" >/dev/null
fingerprint_before=$(sed -n 's/^fingerprint //p' "$cache_abi/base.zti")
mkdir "$work/dist-v2"
printf 'pub def value(): Int = 40\npub def extra(): Int = 2\n' \
    > "$work/source/base.zeta"
"$compiler" --build-library "$work/source/base.zeta" -o "$work/dist-v2" >/dev/null
if "$compiler" --install-library "$work/dist-v2/base.zti" \
    --library-cache "$work/cache" >"$work/fingerprint-conflict" 2>&1; then
    exit 1
fi
grep -q '\[LIB003\]' "$work/fingerprint-conflict"
test "$(sed -n 's/^fingerprint //p' "$cache_abi/base.zti")" = "$fingerprint_before"
"$compiler" --install-library "$work/dist-v2/base.zti" \
    --library-cache "$work/cache" --force >/dev/null
test "$(sed -n 's/^fingerprint //p' "$cache_abi/base.zti")" != "$fingerprint_before"

# Les paires incomplètes, incohérentes et les objets non ELF sont rejetés.
mkdir "$work/invalid"
cp "$work/dist/base.zti" "$work/invalid/wrong.zti"
cp "$work/dist/base.o" "$work/invalid/wrong.o"
if "$compiler" --install-library "$work/invalid/wrong.zti" \
    --library-cache "$work/cache" >"$work/name-error" 2>&1; then
    exit 1
fi
grep -q '\[LIB001\]' "$work/name-error"

cp "$work/dist/base.zti" "$work/invalid/base.zti"
printf 'not an ELF object\n' > "$work/invalid/base.o"
if "$compiler" --install-library "$work/invalid/base.zti" \
    --library-cache "$work/cache" --force >"$work/abi-error" 2>&1; then
    exit 1
fi
grep -q '\[ABI00[12]\]' "$work/abi-error"

printf 'ZTI 999\n' > "$work/invalid/broken.zti"
cp "$work/dist/base.o" "$work/invalid/broken.o"
if "$compiler" --install-library "$work/invalid/broken.zti" \
    --library-cache "$work/cache" >"$work/interface-error" 2>&1; then
    exit 1
fi
grep -q '\[ZTI001\]' "$work/interface-error"

# Une dépendance installée puis corrompue bloque aussi les installations suivantes.
mkdir "$work/broken-cache"
"$compiler" --install-library "$work/dist/base.zti" \
    --library-cache "$work/broken-cache" >/dev/null
broken_abi=$(find "$work/broken-cache" -mindepth 1 -maxdepth 1 \
    -type d -name 'abi-*')
printf 'broken\n' > "$broken_abi/base.o"
if "$compiler" --install-library "$work/dist/service.zti" \
    --library-cache "$work/broken-cache" >"$work/broken-dependency" 2>&1; then
    exit 1
fi
grep -q '\[LIB002\]' "$work/broken-dependency"

# Aucun échec ne doit laisser un fichier de publication temporaire ou de sauvegarde.
test -z "$(find "$work/cache" "$work/broken-cache" -type f \
    \( -name '*.tmp.*' -o -name '*.bak.*' \) -print -quit)"

if "$compiler" --force "$work/consumer/main.zeta" \
    -o "$work/invalid-force" >/dev/null 2>&1; then
    exit 1
fi

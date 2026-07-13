#!/usr/bin/env bash
set -euo pipefail

compiler=$1
stdlib_sources=$2
io_program=$3
collections_program=$4
vectors_program=$5
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/stdlib"
cp "$stdlib_sources"/*.zeta "$work/stdlib/"

"$compiler" --build-stdlib --stdlib "$work/stdlib" >/dev/null
test -f "$work/stdlib/precompiled/manifest"
test -f "$work/stdlib/precompiled/io.o"
test -f "$work/stdlib/precompiled/io.zti"
test -f "$work/stdlib/precompiled/collections.o"
test -f "$work/stdlib/precompiled/collections.zti"
test -f "$work/stdlib/precompiled/vectors.o"
test -f "$work/stdlib/precompiled/vectors.zti"
test ! -e "$work/stdlib/precompiled/.stdlib-build.modules"

# Les deux modules doivent fonctionner sans aucun source standard.
mkdir "$work/sources"
mv "$work/stdlib"/*.zeta "$work/sources/"
"$compiler" "$io_program" -o "$work/io-app" --stdlib "$work/stdlib" >/dev/null
"$work/io-app" >/dev/null
"$compiler" "$collections_program" -o "$work/collections-app" --stdlib "$work/stdlib" >/dev/null
"$work/collections-app"
"$compiler" "$vectors_program" -o "$work/vectors-app" --stdlib "$work/stdlib" >/dev/null
"$work/vectors-app"

# Une source modifiée invalide son artefact partagé et force le fallback source.
mv "$work/sources/io.zeta" "$work/stdlib/io.zeta"
printf '\npub native def precompileProbe(): Int\n' >> "$work/stdlib/io.zeta"
"$compiler" "$io_program" -o "$work/io-fallback" --stdlib "$work/stdlib" >/dev/null
grep -q 'precompileProbe' "$work/io-fallback.modules/io.zti"

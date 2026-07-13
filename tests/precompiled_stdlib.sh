#!/usr/bin/env bash
set -euo pipefail

compiler=$1
stdlib_sources=$2
vec_fixture=$3
io_program=$4
collections_program=$5
vec_program=$6
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/stdlib"
cp "$stdlib_sources"/*.zeta "$work/stdlib/"
cp "$vec_fixture" "$work/stdlib/"

"$compiler" --build-stdlib --stdlib "$work/stdlib" >/dev/null
test -f "$work/stdlib/precompiled/manifest"
test -f "$work/stdlib/precompiled/io.o"
test -f "$work/stdlib/precompiled/io.zti"
test -f "$work/stdlib/precompiled/collections.o"
test -f "$work/stdlib/precompiled/collections.zti"
test -f "$work/stdlib/precompiled/vec_generic_fixture.o"
test -f "$work/stdlib/precompiled/vec_generic_fixture.zti"
test ! -e "$work/stdlib/precompiled/.stdlib-build.modules"

# Les deux modules doivent fonctionner sans aucun source standard.
mkdir "$work/sources"
mv "$work/stdlib"/*.zeta "$work/sources/"
"$compiler" "$io_program" -o "$work/io-app" --stdlib "$work/stdlib" >/dev/null
"$work/io-app" >/dev/null
"$compiler" "$collections_program" -o "$work/collections-app" --stdlib "$work/stdlib" >/dev/null
"$work/collections-app"
"$compiler" "$vec_program" -o "$work/vec-fixture-app" --stdlib "$work/stdlib" >/dev/null
"$work/vec-fixture-app"

# Une source modifiée invalide son artefact partagé et force le fallback source.
mv "$work/sources/io.zeta" "$work/stdlib/io.zeta"
printf '\npub native def precompileProbe(): Int\n' >> "$work/stdlib/io.zeta"
"$compiler" "$io_program" -o "$work/io-fallback" --stdlib "$work/stdlib" >/dev/null
grep -q 'precompileProbe' "$work/io-fallback.modules/io.zti"

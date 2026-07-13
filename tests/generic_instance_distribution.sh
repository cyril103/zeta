#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Deux bibliothèques autonomes publient la même instance faible canonique.
mkdir -p "$work/separate/source" "$work/separate/dist" "$work/separate/consumer"
cp "$fixtures"/*.zeta "$work/separate/source/"
for module in api left right; do
    "$compiler" --build-library "$work/separate/source/$module.zeta" \
        -o "$work/separate/dist" >/dev/null
done
left_symbol=$(readelf -Ws "$work/separate/dist/left.o" |
    awk '/WEAK.*zeta_fn_api__identity__Int__g/ { print $8 }')
right_symbol=$(readelf -Ws "$work/separate/dist/right.o" |
    awk '/WEAK.*zeta_fn_api__identity__Int__g/ { print $8 }')
test -n "$left_symbol"
test "$left_symbol" = "$right_symbol"
cp "$work/separate/dist"/* "$work/separate/consumer/"
cp "$work/separate/source/main.zeta" "$work/separate/consumer/"
"$compiler" "$work/separate/consumer/main.zeta" \
    -o "$work/separate/consumer/app" >/dev/null
"$work/separate/consumer/app"

# Les mêmes paires restent dédupliquées lorsqu'elles proviennent du cache partagé.
for module in api left right; do
    "$compiler" --install-library "$work/separate/dist/$module.zti" \
        --library-cache "$work/cache" >/dev/null
done
mkdir "$work/cache-consumer"
cp "$work/separate/source/main.zeta" "$work/cache-consumer/"
"$compiler" "$work/cache-consumer/main.zeta" -o "$work/cache-consumer/app" \
    --library-cache "$work/cache" >/dev/null
"$work/cache-consumer/app"

# Deux types homonymes de modules distincts doivent produire deux identités.
mkdir "$work/qualified"
printf 'pub def marker[T](value: T): Int = 0\n' > "$work/qualified/generic_api.zeta"
printf 'pub struct Point { x: Int }\n' > "$work/qualified/types_a.zeta"
printf 'pub struct Point { x: Int }\n' > "$work/qualified/types_b.zeta"
printf 'import generic_api\nimport types_a\npub def left(): Int = generic_api.marker[types_a.Point](types_a.Point { x: 1 })\n' \
    > "$work/qualified/left.zeta"
printf 'import generic_api\nimport types_b\npub def right(): Int = generic_api.marker[types_b.Point](types_b.Point { x: 2 })\n' \
    > "$work/qualified/right.zeta"
printf 'import left\nimport right\ndef main(): Int = left.left() + right.right()\n' \
    > "$work/qualified/main.zeta"
"$compiler" "$work/qualified/main.zeta" -o "$work/qualified/app" >/dev/null
"$work/qualified/app"
qualified_count=$(nm --defined-only "$work/qualified/app.modules"/*.o |
    grep -c ' zeta_fn_generic_api__marker__Point__g')
test "$qualified_count" -eq 2

# Deux producteurs homonymes ne partagent jamais leur instance.
mkdir "$work/producers"
printf 'pub def identity[T](value: T): T = value\n' > "$work/producers/api_a.zeta"
printf 'pub def identity[T](value: T): T = value\n' > "$work/producers/api_b.zeta"
printf 'import api_a\npub def left(): Int = api_a.identity[Int](20)\n' \
    > "$work/producers/left.zeta"
printf 'import api_b\npub def right(): Int = api_b.identity[Int](22)\n' \
    > "$work/producers/right.zeta"
printf 'import left\nimport right\ndef main(): Int = left.left() + right.right() - 42\n' \
    > "$work/producers/main.zeta"
"$compiler" "$work/producers/main.zeta" -o "$work/producers/app" >/dev/null
"$work/producers/app"
nm --defined-only "$work/producers/app.modules"/*.o |
    grep ' zeta_fn_api_a__identity__Int__g' >/dev/null
nm --defined-only "$work/producers/app.modules"/*.o |
    grep ' zeta_fn_api_b__identity__Int__g' >/dev/null

# Deux empreintes du même producteur coexistent sans fusion silencieuse.
mkdir -p "$work/versions/v1" "$work/versions/v2" "$work/versions/dist1" \
    "$work/versions/dist2" "$work/versions/consumer"
printf 'pub def identity[T](value: T): T = value\n' > "$work/versions/v1/api.zeta"
printf 'import api\npub def left(): Int = api.identity[Int](20)\n' \
    > "$work/versions/v1/left.zeta"
printf 'pub def identity[T](value: T): T = if (true) value else value\n' \
    > "$work/versions/v2/api.zeta"
printf 'import api\npub def right(): Int = api.identity[Int](22)\n' \
    > "$work/versions/v2/right.zeta"
"$compiler" --build-library "$work/versions/v1/api.zeta" \
    -o "$work/versions/dist1" >/dev/null
"$compiler" --build-library "$work/versions/v1/left.zeta" \
    -o "$work/versions/dist1" >/dev/null
"$compiler" --build-library "$work/versions/v2/api.zeta" \
    -o "$work/versions/dist2" >/dev/null
"$compiler" --build-library "$work/versions/v2/right.zeta" \
    -o "$work/versions/dist2" >/dev/null
v1_symbol=$(readelf -Ws "$work/versions/dist1/left.o" |
    awk '/WEAK.*zeta_fn_api__identity__Int__g/ { print $8 }')
v2_symbol=$(readelf -Ws "$work/versions/dist2/right.o" |
    awk '/WEAK.*zeta_fn_api__identity__Int__g/ { print $8 }')
test -n "$v1_symbol"
test -n "$v2_symbol"
test "$v1_symbol" != "$v2_symbol"
cp "$work/versions/dist1/left."* "$work/versions/consumer/"
cp "$work/versions/dist2/right."* "$work/versions/consumer/"
cp "$work/versions/dist2/api."* "$work/versions/consumer/"
printf 'import left\nimport right\ndef main(): Int = left.left() + right.right() - 42\n' \
    > "$work/versions/consumer/main.zeta"
"$compiler" "$work/versions/consumer/main.zeta" \
    -o "$work/versions/consumer/app" >/dev/null
"$work/versions/consumer/app"

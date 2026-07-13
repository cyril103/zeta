#!/usr/bin/env bash
set -euo pipefail

compiler=$1
stdlib=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/source" "$work/published" "$work/consumer"
printf 'pub def answer(): Int = 42\n' > "$work/source/answers.zeta"

"$compiler" --build-library "$work/source/answers.zeta" \
    -o "$work/published" >/dev/null
test -f "$work/published/answers.zti"
test -f "$work/published/answers.o"
test "$(find "$work/published" -maxdepth 1 -type f | wc -l)" -eq 2

# La paire publiée doit suffire : le consommateur ne voit plus le source.
rm "$work/source/answers.zeta"
cp "$work/published/answers.zti" "$work/consumer/answers.zti"
cp "$work/published/answers.o" "$work/consumer/answers.o"
printf 'import answers\ndef main(): Int = answers.answer() - 42\n' \
    > "$work/consumer/main.zeta"
"$compiler" "$work/consumer/main.zeta" -o "$work/consumer/app" >/dev/null
"$work/consumer/app"

# Le dossier de sortie est obligatoire et ne peut pas être un fichier.
if "$compiler" --build-library "$work/consumer/main.zeta" >/dev/null 2>&1; then
    exit 1
fi
touch "$work/not-a-directory"
if "$compiler" --build-library "$work/consumer/main.zeta" \
    -o "$work/not-a-directory" >"$work/output-error" 2>&1; then
    exit 1
fi
grep -q 'doit être un dossier' "$work/output-error"

# Les deux modes de construction de bibliothèque sont mutuellement exclusifs.
if "$compiler" --build-library --build-stdlib "$work/consumer/main.zeta" \
    -o "$work/conflicting" >/dev/null 2>&1; then
    exit 1
fi

# Les corps génériques publics restent instanciables sans le source producteur.
mkdir -p "$work/generic-source" "$work/generic-published" "$work/generic-consumer"
printf 'pub def identity[T](value: T): T = value\n' \
    > "$work/generic-source/generic_api.zeta"
"$compiler" --build-library "$work/generic-source/generic_api.zeta" \
    -o "$work/generic-published" >/dev/null
grep -q '^generic_tokens 1 ' "$work/generic-published/generic_api.zti"
rm "$work/generic-source/generic_api.zeta"
cp "$work/generic-published/generic_api.zti" "$work/generic-consumer/generic_api.zti"
cp "$work/generic-published/generic_api.o" "$work/generic-consumer/generic_api.o"
printf 'import generic_api\ndef main(): Int = generic_api.identity[Int](0)\n' \
    > "$work/generic-consumer/main.zeta"
"$compiler" "$work/generic-consumer/main.zeta" \
    -o "$work/generic-consumer/app" >/dev/null
"$work/generic-consumer/app"

# Les dépendances sont décrites, mais restent des paires publiées séparément.
mkdir -p "$work/dependency-source" "$work/dependency-published" \
    "$work/dependency-consumer"
printf 'pub def base(): Int = 40\n' > "$work/dependency-source/base.zeta"
printf 'import base\npub def answer(): Int = base.base() + 2\n' \
    > "$work/dependency-source/service.zeta"
"$compiler" --build-library "$work/dependency-source/base.zeta" \
    -o "$work/dependency-published" >/dev/null
"$compiler" --build-library "$work/dependency-source/service.zeta" \
    -o "$work/dependency-published" >/dev/null
grep -q '^import "base"$' "$work/dependency-published/service.zti"
rm "$work/dependency-source/base.zeta" "$work/dependency-source/service.zeta"
cp "$work/dependency-published"/* "$work/dependency-consumer/"
printf 'import service\ndef main(): Int = service.answer() - 42\n' \
    > "$work/dependency-consumer/main.zeta"
"$compiler" "$work/dependency-consumer/main.zeta" \
    -o "$work/dependency-consumer/app" >/dev/null
"$work/dependency-consumer/app"

# L'implémentation runtime native est fusionnée dans l'unique objet publié.
mkdir -p "$work/native-published" "$work/native-consumer"
"$compiler" --build-library "$stdlib/io.zeta" \
    -o "$work/native-published" >/dev/null
test "$(find "$work/native-published" -maxdepth 1 -type f | wc -l)" -eq 2
cp "$work/native-published/io.zti" "$work/native-consumer/io.zti"
cp "$work/native-published/io.o" "$work/native-consumer/io.o"
printf 'import io\ndef main(): Int = io.print("")\n' \
    > "$work/native-consumer/main.zeta"
"$compiler" "$work/native-consumer/main.zeta" \
    -o "$work/native-consumer/app" >/dev/null
"$work/native-consumer/app"

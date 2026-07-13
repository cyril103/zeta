#!/usr/bin/env bash
set -euo pipefail

compiler=$1
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

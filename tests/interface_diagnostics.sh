#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$compiler" "$fixtures/main.zeta" -o "$work/published" >/dev/null
baseline_interface="$work/published.modules/types.zti"
baseline_object="$work/published.modules/types.o"

prepare() {
    case_name=$1
    mkdir "$work/$case_name"
    cp "$fixtures/main.zeta" "$work/$case_name/main.zeta"
    cp "$baseline_interface" "$work/$case_name/types.zti"
    cp "$baseline_object" "$work/$case_name/types.o"
}

rejects() {
    case_name=$1
    code=$2
    if "$compiler" "$work/$case_name/main.zeta" -o "$work/$case_name/app" \
        >"$work/$case_name/error" 2>&1; then
        exit 1
    fi
    grep -q "\[$code\]" "$work/$case_name/error"
    grep -q 'types\.zt\|types\.o' "$work/$case_name/error"
}

prepare version
sed -i '1s/ZTI 11/ZTI 999/' "$work/version/types.zti"
rejects version ZTI001

prepare syntax
sed -i '/^end$/iunknown_entry' "$work/syntax/types.zti"
rejects syntax ZTI010

prepare nominal
sed -i 's/field "x" 0 "I"/field "x" 0 "U7:Missing[0:]"/' \
    "$work/nominal/types.zti"
rejects nominal ZTI100
grep -q "structure 'Point', champ 'x'" "$work/nominal/error"

prepare structure-layout
sed -i 's/structure "Point" 4 4/structure "Point" 0 4/' \
    "$work/structure-layout/types.zti"
rejects structure-layout ZTI200
grep -q "structure 'Point', champ 'x'" "$work/structure-layout/error"

prepare enum-layout
sed -i 's/variant "Ok" 4 4 1/variant "Ok" 0 4 1/' \
    "$work/enum-layout/types.zti"
rejects enum-layout ZTI200
grep -q "énumération 'Result', variante 'Ok', champ 'value'" \
    "$work/enum-layout/error"

prepare generic
sed -i 's/^generic_tokens 3 /generic_tokens 999 /' "$work/generic/types.zti"
rejects generic ZTI300

prepare module-name
sed -i 's/^module "types"/module "other"/' "$work/module-name/types.zti"
rejects module-name MOD002

prepare missing-object
rm "$work/missing-object/types.o"
rejects missing-object MOD003

prepare invalid-elf
printf 'not an elf object' > "$work/invalid-elf/types.o"
rejects invalid-elf ABI001

prepare wrong-architecture
printf '\050' | dd of="$work/wrong-architecture/types.o" bs=1 seek=18 conv=notrunc \
    status=none
rejects wrong-architecture ABI002

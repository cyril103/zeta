#!/usr/bin/env bash
set -euo pipefail

compiler=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$fixtures"/*.zeta "$work/"

if "$compiler" "$work/main.zeta" -o "$work/app" >"$work/error" 2>&1; then
    exit 1
fi
grep -q "multiple definition.*zeta_fn_api__identity__Int__g" "$work/error"

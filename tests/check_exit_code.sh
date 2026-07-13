#!/usr/bin/env bash
set -u

"$1"
status=$?
if [[ "$status" -ne "$2" ]]; then
    echo "code de sortie attendu: $2, reçu: $status" >&2
    exit 1
fi

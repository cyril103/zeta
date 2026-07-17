#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
source="$2"
output="$3"
rm -f "${output}" "${output}.ll" "${output}.ir" "${output}.diag" "${output}.stdout"

"${compiler}" "${source}" --backend=clang -o "${output}" >/dev/null

grep -Eq '@slot[0-9]+ = global \{ ptr, i64 \} zeroinitializer' "${output}.ll"
grep -q '@zeta_rt_string_length_bytes(ptr' "${output}.ll"

set +e
"${output}" >"${output}.stdout"
status=$?
set -e
if [ "${status}" -ne 4 ]; then
    echo "unexpected exit status ${status}" >&2
    cat "${output}.stdout" >&2
    exit 1
fi
grep -qx 'zeta' "${output}.stdout"

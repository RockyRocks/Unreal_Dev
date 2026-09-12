#!/usr/bin/env bash
# Merge Clang 18 IR-PGO raw profiles. Do not pass -sparse (that is for coverage).
set -euo pipefail

PGO_DIR="${UE_PGO_DIR:?UE_PGO_DIR is required}"
RAW_DIR="${PGO_DIR}/raw"
OUT="${PGO_DIR}/default.profdata"
PROFDATA="${LLVM_PROFDATA:-llvm-profdata-18}"

if ! command -v "${PROFDATA}" >/dev/null 2>&1; then
  PROFDATA="llvm-profdata"
fi

shopt -s nullglob
raws=("${RAW_DIR}"/*.profraw)
if [[ ${#raws[@]} -eq 0 ]]; then
  echo "error: no *.profraw under ${RAW_DIR}" >&2
  exit 1
fi

mkdir -p "${PGO_DIR}"
"${PROFDATA}" merge --num-threads=0 -output="${OUT}" "${raws[@]}"

echo "=== profile summary ==="
"${PROFDATA}" show --detailed-summary "${OUT}"

# Reject empty / broken profiles before the expensive Phase B compile.
bytes="$(wc -c < "${OUT}")"
if [[ "${bytes}" -lt 65536 ]]; then
  echo "error: ${OUT} is only ${bytes} bytes; training likely failed to flush" >&2
  exit 1
fi

echo "wrote ${OUT} (${bytes} bytes) from ${#raws[@]} raw files"

#!/usr/bin/env bash
# Strip the store copy AFTER sentry-cli upload of the unstripped pair.
# Does not rewrite instructions; debug-id stays stable.
set -euo pipefail

BIN="${1:?usage: strip-shipping.sh <exe-or-elf>}"
OBJCOPY="${LLVM_OBJCOPY:-llvm-objcopy-18}"
STRIP="${LLVM_STRIP:-llvm-strip-18}"

if ! command -v "${OBJCOPY}" >/dev/null 2>&1; then
  OBJCOPY="llvm-objcopy"
fi
if ! command -v "${STRIP}" >/dev/null 2>&1; then
  STRIP="llvm-strip"
fi

if [[ ! -f "${BIN}" ]]; then
  echo "error: ${BIN} not found" >&2
  exit 1
fi

case "${BIN}" in
  *.exe|*.dll)
    "${OBJCOPY}" --strip-unneeded "${BIN}"
    ;;
  *)
    if [[ -f "${BIN}.debug" ]]; then
      "${OBJCOPY}" --add-gnu-debuglink="${BIN}.debug" "${BIN}" || true
    fi
    "${STRIP}" --strip-unneeded "${BIN}"
    ;;
esac

echo "stripped ${BIN}"

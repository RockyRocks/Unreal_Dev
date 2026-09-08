#!/usr/bin/env bash
# Upload the UNSTRIPPED Phase B binary + debug companion, then finalize the release.
# Call this BEFORE strip-shipping.sh.
set -euo pipefail

: "${SENTRY_ORG:?}"
: "${SENTRY_PROJECT:?}"
: "${SENTRY_AUTH_TOKEN:?}"
: "${SENTRY_RELEASE:?}"

SYMBOLS_DIR="${1:?usage: upload-sentry-symbols.sh <symbols-dir>}"
CLI="${SENTRY_CLI:-sentry-cli}"

if [[ ! -d "${SYMBOLS_DIR}" ]]; then
  echo "error: ${SYMBOLS_DIR} does not exist" >&2
  exit 1
fi

"${CLI}" releases new "${SENTRY_RELEASE}" || true
"${CLI}" releases set-commits "${SENTRY_RELEASE}" --auto || true

# Include the executable itself so Sentry has CFI for PGO/ThinLTO frames.
"${CLI}" debug-files upload --wait "${SYMBOLS_DIR}"

"${CLI}" releases finalize "${SENTRY_RELEASE}"
echo "uploaded symbols for ${SENTRY_RELEASE} from ${SYMBOLS_DIR}"

#!/usr/bin/env bash
#
# run_emit_tests.sh - end-to-end emit regression: compile the psi benchmark,
# emit its AP3 manifest via the RevealView pass, and assert the manifest_hash
# equals the cross-language reference (the value ap3-manifest / Python produces
# for the same logical manifest). Runs in the hermetic image and in CI.
set -euo pipefail

SMAUG_HOME="${SMAUG_HOME:-/opt/smaug}"
EXPECTED="2ce1e7a250d66910b3f8d303c70f991aef414ea0cbb7ec5add1c540c0d813b7b"
out="$(mktemp -d)/psi.manifest.json"

"${SMAUG_HOME}/scripts/emit_manifest.sh" \
    "${SMAUG_HOME}/benchmarks/psi.cpp" \
    "${SMAUG_HOME}/benchmarks/metadata/psi.ll.json" \
    "$out" >/dev/null

got="$(grep -o '"manifest_hash":"[0-9a-f]*"' "$out" | cut -d'"' -f4)"
if [ "$got" != "$EXPECTED" ]; then
  echo "[FAIL] emit: psi manifest_hash mismatch"
  echo "  want: $EXPECTED"
  echo "  got:  $got"
  exit 1
fi
echo "[PASS] emit: psi manifest_hash=$got"

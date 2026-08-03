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

# --- Tier 1: a CUSTOM program (not a bundled benchmark) compiles + emits ------
CUSTOM_EXPECTED="789fc8d0546444100813375653e8e44b874198d284c88de4ca7f2e215cb8994a"
tmp="$(mktemp -d)"
cat > "$tmp/prog.cpp" <<'CPP'
#include "mpc/mpc.h"
void combined_exposure(int *A, int *B, int N, int limit, int *over) {
  int s = 0; for (int i = 0; i < N; ++i) s += A[i] + B[i]; over[0] = (s > limit);
}
int main() { return 0; }
CPP
cat > "$tmp/prog.ll.json" <<'JSON'
{"_Z17combined_exposurePiS_iiS_":{"input":[1,1,0,0,1],"output":0,"ap3":{
  "entry":true,"program":"combined_exposure",
  "inputs":[{"arg":0,"name":"A_amounts","party":"A","dtype":"i32[]"},
            {"arg":1,"name":"B_amounts","party":"B","dtype":"i32[]"},
            {"arg":2,"name":"N","party":"public","dtype":"i32"},
            {"arg":3,"name":"limit","party":"public","dtype":"i32"}],
  "outputs":[{"arg":4,"name":"over_limit","dtype":"bool","reveal_to":"both",
    "derivation":"combined exposure over limit",
    "leaks":[{"kind":"comparison_result","to":"both","description":"one bit"}]}],
  "summary":"combined exposure threshold"}}}
JSON
"${SMAUG_HOME}/scripts/emit_manifest.sh" "$tmp/prog.cpp" "$tmp/prog.ll.json" "$tmp/prog.manifest.json" >/dev/null
cgot="$(grep -o '"manifest_hash":"[0-9a-f]*"' "$tmp/prog.manifest.json" | cut -d'"' -f4)"
if [ "$cgot" != "$CUSTOM_EXPECTED" ]; then
  echo "[FAIL] emit: custom program manifest_hash mismatch"
  echo "  want: $CUSTOM_EXPECTED"
  echo "  got:  $cgot"
  exit 1
fi
echo "[PASS] emit: custom program manifest_hash=$cgot"

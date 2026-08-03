#!/usr/bin/env bash
#
# determinism_check.sh — the AP3 determinism gate.
#
# AP3 cross-org consensus rests on one claim: identical source + identical toolchain
# produces an identical manifest. Two independently operated pods compile the same
# program and consent only if `manifest_hash` and `toolchain_ver` agree. If compilation
# is not deterministic, that agreement is accidental and the consensus design is void.
#
# This script asserts the claim two ways:
#
#   1. WITHIN one container — compile each program N times; every emitted manifest must
#      be byte-identical. Catches nondeterminism from hash-map iteration order, ASLR,
#      timestamps, temp paths, uninitialised padding.
#   2. ACROSS two containers — `make determinism` runs this twice into separate output
#      dirs and then re-invokes with --compare. Catches per-instance state (hostname,
#      PID, container-id, mount paths) leaking into the output.
#
# It also records IR-level digests as a DIAGNOSTIC. The manifest is derived from the
# semantic structure rather than the IR text, so IR drift does not necessarily break
# consensus — but it localises the cause when a manifest does drift, which is the
# "identify nondeterminism sources" half of the spike.
#
# Runs INSIDE the hermetic image against the image's own built artifacts. Do NOT
# bind-mount the repo over /opt/smaug: host build dirs (a macOS libap3pass.so, say)
# would shadow the Linux ones and the gate would be testing the wrong toolchain.
#
#   determinism_check.sh                 # run the gate, write digests to $DET_OUT
#   determinism_check.sh --compare A B   # diff two runs' digest files
#
# Env: DET_N (default 5), DET_OUT (default /out/run-$(hostname))

set -euo pipefail

SMAUG_HOME="${SMAUG_HOME:-/opt/smaug}"
CLANG="${CLANG:-/usr/lib/llvm-18/bin/clang++}"
STRIP="${SMAUG_HOME}/passes/remove_target_triple.py"
EMIT="${SMAUG_HOME}/scripts/emit_manifest.sh"
BENCH="${SMAUG_HOME}/benchmarks"
META="${BENCH}/metadata"

# Programs carrying an "ap3" annotation block, so the pass emits a manifest. These are
# the ones consensus actually depends on.
MANIFEST_PROGS="${DET_MANIFEST_PROGS:-psi psi_cardinality}"
# Additional programs compiled for the IR-level diagnostic only (no ap3 block → no
# manifest). Widens the sample for detecting compiler nondeterminism.
IR_ONLY_PROGS="${DET_IR_PROGS:-inner_product histogram}"

N="${DET_N:-5}"
OUT="${DET_OUT:-/out/run-$(hostname)}"

red()  { printf '\033[31m%s\033[0m\n' "$*"; }
green(){ printf '\033[32m%s\033[0m\n' "$*"; }

# ---------------------------------------------------------------- compare mode
if [ "${1:-}" = "--compare" ]; then
    A="${2:?usage: --compare <dirA> <dirB>}"
    B="${3:?usage: --compare <dirA> <dirB>}"
    echo "== cross-instance comparison =="
    if diff -u "$A/digests.txt" "$B/digests.txt"; then
        green "[PASS] cross-instance: $(basename "$A") and $(basename "$B") are identical"
        exit 0
    fi
    red "[FAIL] cross-instance: digests differ between container instances"
    red "       Output depends on per-instance state, not just source + toolchain."
    exit 1
fi

# ------------------------------------------------------------------- run mode
mkdir -p "$OUT"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
digests="$OUT/digests.txt"
: > "$digests"

fail=0

# The toolchain identity itself is part of the claim — two runs that agree on manifests
# but disagree here are NOT interchangeable, so it belongs in the compared record.
if [ -f "${SMAUG_HOME}/toolchain.lock" ]; then
    tv="$(grep -o '"toolchain_ver": *"[^"]*"' "${SMAUG_HOME}/toolchain.lock" | cut -d'"' -f4)"
    echo "toolchain_ver ${tv}" >> "$digests"
    echo "toolchain_ver = ${tv}"
fi
echo "N=${N} out=${OUT}"
echo

# --- 1. manifest determinism (the load-bearing assertion) --------------------
echo "== manifest determinism (${N}x per program, same container) =="
for prog in $MANIFEST_PROGS; do
    src="${BENCH}/${prog}.cpp"
    meta="${META}/${prog}.ll.json"
    if [ ! -f "$src" ] || [ ! -f "$meta" ]; then
        red "[SKIP] ${prog}: missing source or metadata"
        continue
    fi
    for i in $(seq 1 "$N"); do
        "$EMIT" "$src" "$meta" "$work/${prog}.${i}.json" >/dev/null
    done
    uniq_count="$(sha256sum "$work/${prog}".*.json | awk '{print $1}' | sort -u | wc -l)"
    sha="$(sha256sum "$work/${prog}.1.json" | awk '{print $1}')"
    mh="$(grep -o '"manifest_hash":"[0-9a-f]*"' "$work/${prog}.1.json" | cut -d'"' -f4)"
    if [ "$uniq_count" -ne 1 ]; then
        red "[FAIL] ${prog}: ${uniq_count} distinct manifests across ${N} compiles"
        diff <(head -c 2000 "$work/${prog}.1.json") <(head -c 2000 "$work/${prog}.2.json") || true
        fail=1
    else
        green "[PASS] ${prog}: ${N}/${N} byte-identical  manifest_hash=${mh}"
    fi
    echo "manifest ${prog} ${sha} ${mh}" >> "$digests"
done
echo

# --- 2. custom-source path (what Tier-1 ingress will compile) ----------------
# A bundled benchmark is compiled from a fixed path; arbitrary source arrives in a
# fresh temp dir each time. That path difference is a classic nondeterminism source,
# so exercise it explicitly rather than assuming the benchmarks generalise.
echo "== custom-source determinism (${N}x, fresh temp dir each time) =="
custom_shas=""
for i in $(seq 1 "$N"); do
    cdir="$(mktemp -d)"
    cat > "$cdir/prog.cpp" <<'CPP'
#include "mpc/mpc.h"
void combined_exposure(int *A, int *B, int N, int limit, int *over) {
  int s = 0; for (int i = 0; i < N; ++i) s += A[i] + B[i]; over[0] = (s > limit);
}
int main() { return 0; }
CPP
    cat > "$cdir/prog.ll.json" <<'JSON'
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
    "$EMIT" "$cdir/prog.cpp" "$cdir/prog.ll.json" "$work/custom.${i}.json" >/dev/null
    rm -rf "$cdir"
done
uniq_count="$(sha256sum "$work/custom".*.json | awk '{print $1}' | sort -u | wc -l)"
sha="$(sha256sum "$work/custom.1.json" | awk '{print $1}')"
mh="$(grep -o '"manifest_hash":"[0-9a-f]*"' "$work/custom.1.json" | cut -d'"' -f4)"
if [ "$uniq_count" -ne 1 ]; then
    red "[FAIL] custom: ${uniq_count} distinct manifests across ${N} compiles"
    red "       Build-path or temp-dir state is leaking into the manifest."
    fail=1
else
    green "[PASS] custom: ${N}/${N} byte-identical  manifest_hash=${mh}"
fi
echo "manifest custom ${sha} ${mh}" >> "$digests"
echo

# --- 3. IR-level diagnostic (non-fatal) --------------------------------------
# Reported, not enforced: the manifest is a semantic derivation, so IR text drift is
# tolerable. It is recorded because it is the first place to look when a manifest drifts.
echo "== IR determinism (diagnostic, non-fatal) =="
for prog in $MANIFEST_PROGS $IR_ONLY_PROGS; do
    src="${BENCH}/${prog}.cpp"
    [ -f "$src" ] || { red "[SKIP] ${prog}: no source"; continue; }
    for i in $(seq 1 "$N"); do
        "$CLANG" -I"${SMAUG_HOME}/runtime" -O0 -Xclang -disable-O0-optnone \
            -S -emit-llvm -std=c++17 "$src" -o "$work/${prog}.${i}.ll" 2>/dev/null
        [ -f "$STRIP" ] && python3 "$STRIP" "$work/${prog}.${i}.ll" || true
    done
    uniq_count="$(sha256sum "$work/${prog}".*.ll | awk '{print $1}' | sort -u | wc -l)"
    sha="$(sha256sum "$work/${prog}.1.ll" | awk '{print $1}')"
    if [ "$uniq_count" -ne 1 ]; then
        echo "[WARN] ${prog}: IR differs across compiles (${uniq_count} variants) — first diff:"
        diff "$work/${prog}.1.ll" "$work/${prog}.2.ll" | head -12 || true
    else
        green "[ok]   ${prog}: IR byte-identical"
    fi
    echo "ir ${prog} ${sha}" >> "$digests"
done
echo

sort -o "$digests" "$digests"
echo "digests → $digests"
if [ "$fail" -ne 0 ]; then
    red "[FAIL] determinism gate failed"
    exit 1
fi
green "[PASS] determinism gate: all manifests byte-identical within this container"

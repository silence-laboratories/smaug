#!/usr/bin/env bash
#
# emit_manifest.sh - compile a program and emit its AP3 manifest.
#
#   emit_manifest.sh <source.cpp> <metadata.ll.json> [out.manifest.json]
#
# Runs: clang -emit-llvm -> strip target triple -> opt (ap3-reveal-view pass) ->
# manifest.json. Toolchain paths default to the hermetic image layout and can be
# overridden by env vars for local runs (e.g. against Homebrew llvm@18).
set -euo pipefail

SRC="${1:?usage: emit_manifest.sh <source.cpp> <metadata.ll.json> [out.json]}"
META="${2:?metadata path required}"
OUT="${3:-${SRC##*/}.manifest.json}"

SMAUG_HOME="${SMAUG_HOME:-/opt/smaug}"
CLANG="${CLANG:-/usr/lib/llvm-18/bin/clang++}"
OPT="${OPT:-/usr/lib/llvm-18/bin/opt}"
PLUGIN="${AP3_PLUGIN:-${SMAUG_HOME}/passes/ap3/build/libap3pass.so}"
PASSES="${AP3_PASSES:-ap3-reveal-view}"
INCLUDE="${SMAUG_INCLUDE:-${SMAUG_HOME}/runtime}"
STRIP="${SMAUG_HOME}/passes/remove_target_triple.py"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
ll="$work/mod.ll"

"$CLANG" -I"$INCLUDE" -O0 -Xclang -disable-O0-optnone -S -emit-llvm -std=c++17 "$SRC" -o "$ll"
[ -f "$STRIP" ] && python3 "$STRIP" "$ll" || true

"$OPT" -load-pass-plugin="$PLUGIN" -passes="$PASSES" \
    --ap3-metadata-path="$META" \
    --ap3-manifest-out="$OUT" \
    --disable-output "$ll"

echo "wrote $OUT"

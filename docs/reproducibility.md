# Reproducible toolchain & `toolchain_ver`

AP3 cross-org consensus rests on a claim: **identical `source + toolchain` produces an
identical manifest.** If the toolchain is not reproducible, that claim is unfounded and the
consensus design is invalid. This document describes how Smaug's build is pinned, how
`toolchain_ver` is derived, and the known gaps.

## Why this matters

Two independently operated pods each compile the *same* source with the *same* toolchain and
must agree on `manifest_hash` and `toolchain_ver` (see `ap3-pod` consensus). `manifest_hash`
is a semantic hash of the AP3 manifest; `toolchain_ver` is an opaque equality anchor for the
compiler that produced it. Equality of both is what lets two orgs consent to the *same*
computation without trusting each other's infrastructure.

## The hermetic image

`docker/Dockerfile` builds Smaug in a pinned Ubuntu + LLVM-18 + EMP environment. Nothing on
the host (OS, LLVM version, EMP presence) affects the result. Two targets:

| Target | Contents | Use |
|--------|----------|-----|
| `llvm-dev` | Ubuntu + LLVM-18 + `libpasses.so` | Build the pass plugin, emit manifests (`opt` on `.ll`). Fast; no EMP. |
| `full` | `llvm-dev` + EMP toolkit + built runtime | End-to-end 2PC runs, determinism gate. |

The manifest-emit pass runs entirely in `llvm-dev`: `mpc.h` includes only `<stdint.h>`, so
emitting IR and running `-passes=smaug-pipeline` needs no EMP. EMP is only for the final
link + run of the 2PC executable.

All external inputs are pinned as build args in `docker/toolchain.env`.

## `toolchain_ver` derivation

`scripts/gen_toolchain_lock.sh` runs *inside* the image and records the **resolved** state
into `/opt/smaug/toolchain.lock`:

```json
{
  "toolchain_ver": "smaug-<smaug-sha>+llvm18+emp-0.3.x+<digest16>",
  "toolchain_digest": "<sha256[:16] of all component SHAs>",
  "components": { "ubuntu": "...", "llvm": "...", "smaug": "...",
                  "emp-tool": "<sha>", "emp-ot": "<sha>", "emp-sh2pc": "<sha>", "emp-aby": "<sha>" }
}
```

The `toolchain_ver` string carries a human-readable family (matching the golden-vector
convention `smaug-<ver>+llvm<major>+emp-<x>`) **plus** a digest binding the exact component
commit SHAs. So even if a moving tag (`0.3.x`) advances upstream, the digest changes and two
pods will correctly *fail* to agree rather than silently consenting to different compilers.

## Determinism normalization

Byte-identical LLVM IR across builds requires stripping non-semantic, environment-derived
metadata before hashing (handled by the emit pass + `scripts/determinism_check.sh`):

- **debug info** (`!llvm.dbg.*`, `DILocation`) — embeds source paths and line tables;
- **source filename / module identifier** — absolute build paths;
- **module flags** unrelated to semantics;
- compiler **producer** strings.

The manifest is computed from the *semantic* structure, not the raw IR text, which sidesteps
most textual nondeterminism; the determinism gate still asserts byte-identical manifests.

## Determinism gate

`make determinism` compiles the benchmarks N times within one container and again across two
container instances, then asserts every emitted manifest (and its `manifest_hash`) is
identical. It runs in CI. See the P0 determinism spike in `ap3-internal/design/tasks.md`.

## Known gaps (tracked)

- **Base image / EMP pinned by tag, not digest.** `toolchain.env` uses `ubuntu:24.04` and the
  `0.3.x` EMP tags for fetch convenience. The lock file records resolved SHAs, so a build is
  *auditable*, but true bit-reproducibility wants `BASE_REF=ubuntu@sha256:...` and EMP refs
  pinned to SHAs. Follow-up: cut a `toolchain.env.locked` from a known-good build.
- **No Nix/Bazel yet.** Docker gives a pinned, cached, distributable build; Nix/Bazel would
  make it content-addressed end-to-end (design §3). Docker first, Nix as a follow-up.
- **No image signing / SLSA provenance yet.** cosign + provenance are the supply-chain
  follow-up once the image stabilizes.

# AP3 manifest emission

Smaug **emits into** the AP3 circuit manifest: a derived, party-aware summary of a
compiled MPC program (I/O signature + reveal-view + integrity anchors) that every
downstream component - policy, consensus, UI, audit - consumes *instead of* the
LLVM IR. See the schema in `ap3-manifest`.

## Pieces

| Path | Role |
|------|------|
| `passes/manifest/` | **LLVM-free core**: SHA-256, canonical JSON (byte-identical to ap3-manifest's Python), the schema-v1 model + validation, and the `ap3`-metadata parser + `buildManifest`. Unit-tested (`ctest`). |
| `passes/ap3/RevealView.cpp` | The **LLVM pass** (`ap3-reveal-view`): reads the module + metadata, cross-checks, and writes `manifest.json` fail-closed. A thin adapter over the core. |
| `scripts/emit_manifest.sh` | compile → strip triple → `opt -passes=ap3-reveal-view` → `manifest.json`. |

## The `ap3` metadata block

Party ownership (which physical party owns an input) and reveal targets (who learns
an output) are **policy facts the IR does not carry** - the runtime reveal is
party-blind (`MPCLink::revealOutput` hardcodes party 1). So they are declared in an
`ap3` block added to Smaug's per-function metadata JSON (backward compatible; other
passes ignore it):

```json
"_Z3psiPiS_iS_": {
  "input": [1,1,0,1], "readAccess": {...}, "sizes": {...}, "output": 0,
  "ap3": {
    "entry": true,
    "program": "psi",
    "inputs": [
      { "arg": 0, "name": "X", "party": "A", "dtype": "i32[]" },
      { "arg": 1, "name": "Y", "party": "B", "dtype": "i32[]" },
      { "arg": 2, "name": "N", "party": "public", "dtype": "i32" }
    ],
    "outputs": [
      { "arg": 3, "name": "intersection", "dtype": "i32[]", "reveal_to": "both",
        "derivation": "...", "leaks": [{ "kind": "intersection_elements", "to": "both" }] }
    ],
    "summary": "..."
  }
}
```

- `party` ∈ `A | B | public`; a non-public input is secret-shared (`private: true`).
- `reveal_to` ∈ `A | B | both | none`; `none` outputs are omitted from the reveal-view.
- `dtype` may be omitted for scalar integer args (inferred `iN` from the IR); array /
  pointer dtypes must be declared - LLVM-18 opaque pointers carry no element type.

## Running it

Standalone (what CI and the emit service use):

```bash
opt -load-pass-plugin=libap3pass.so -passes=ap3-reveal-view \
    --ap3-metadata-path=psi.ll.json --ap3-manifest-out=psi.manifest.json \
    --disable-output psi.ll
```

Integrated, in one invocation after the full transform (both plugins loaded):

```bash
opt -load-pass-plugin=libpasses.so -load-pass-plugin=libap3pass.so \
    -passes='smaug-pipeline,ap3-reveal-view' --ap3-metadata-path=... in.ll -o out.ll -S
```

Anchors: `--ap3-source-hash` (the pod's input anchor) and `--ap3-toolchain-ver`
(defaults from `toolchain.lock`) fill `program.*`; they are excluded from
`manifest_hash`.

## Design notes

- **Metadata is authoritative for the semantic view.** The manifest's I/O signature and
  reveal-view come from the `ap3` block, so emission is a module pass that does not
  require the full transform to have run. That keeps emission robust and testable, and
  it is why the pass can run standalone or appended to `smaug-pipeline`.
- **Fail-closed.** Malformed metadata, an out-of-range arg, a missing/inferable-less
  dtype, or a manifest that fails schema/enum/consistency validation all abort emission -
  no manifest is written.
- **Cross-language hash parity is enforced.** `manifest_hash` is byte-identical to the
  Python implementation; the psi manifest hash is pinned in `test_ap3_metadata` and
  `run_emit_tests.sh`, and the golden vectors in `test_conformance`.

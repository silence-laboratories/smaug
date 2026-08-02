// Parse the AP3 semantic annotations (the "ap3" block) from Smaug's per-function
// metadata JSON, and build an AP3 Manifest from them.
//
// The "ap3" block is the authoritative source of the party-aware I/O signature
// and reveal-view: which physical party owns each input and who each output is
// revealed to are policy facts the compiler cannot infer from the IR (the reveal
// mechanism in the runtime is party-blind - see MPCLink revealOutput). So the
// developer/policy declares them here and Smaug emits them into the manifest.
//
// This layer is deliberately LLVM-free: it turns metadata -> Manifest and is unit
// tested natively. The RevealView LLVM pass enriches the spec with IR-derived
// facts (dtype/shape inference, soundness cross-checks) and calls buildManifest.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ap3/Manifest.h"

namespace ap3 {

struct Ap3Input {
  int arg = -1;                 // LLVM argument index this annotates
  std::string name;
  std::string party;            // "A" | "B" | "public"
  std::string dtype;            // explicit dtype, or empty to infer from IR
  std::optional<std::vector<int64_t>> shape;
};

struct Ap3Output {
  int arg = -1;                 // arg index of a written output pointer, or -1 for the return value
  std::string name;
  std::string dtype;
  std::string reveal_to;        // "A" | "B" | "both" | "none"
  std::string derivation;
  std::vector<Leak> leaks;
};

struct Ap3FunctionSpec {
  std::string func_name;        // mangled LLVM function name (the metadata key)
  std::string program;
  bool entry = false;
  std::vector<Ap3Input> inputs;
  std::vector<Ap3Output> outputs;
  std::optional<std::string> summary;
};

// Parse every function's "ap3" block from the metadata JSON text. Keyed by the
// mangled function name. Functions without an "ap3" block are omitted. Throws
// std::runtime_error on malformed JSON or an invalid "ap3" block.
std::map<std::string, Ap3FunctionSpec> parseAp3Metadata(const std::string &jsonText);

// Choose the entry spec: the one with entry==true, else the sole spec. Sets err
// and returns nullopt if there is no unambiguous entry.
std::optional<Ap3FunctionSpec>
selectEntry(const std::map<std::string, Ap3FunctionSpec> &specs, std::string &err);

// Build a Manifest from a spec plus the integrity anchors. Pure: no IR, no I/O.
// The caller is responsible for having filled in dtypes/shapes on the spec.
Manifest buildManifest(const Ap3FunctionSpec &spec, const std::string &source_hash,
                       const std::string &toolchain_ver);

} // namespace ap3

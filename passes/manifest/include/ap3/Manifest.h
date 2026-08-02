// The AP3 circuit manifest, C++ side. Mirrors ap3-manifest/schema/manifest.schema.json
// (schema v1) and reproduces the manifest_hash from canonical.py byte-for-byte.
//
// This is the contract Smaug EMITS INTO. It is a derived summary - party-aware
// I/O + reveal-view + integrity anchors - deliberately insufficient to
// reconstruct or execute the circuit. No gate list, no opcodes, no IR.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ap3/CanonicalJson.h"

namespace ap3 {

struct Input {
  std::string name;
  std::string dtype;                       // e.g. "i32", "i32[]", "bool"
  std::string party;                       // "A" | "B" | "public"
  bool priv = false;                       // private/secret-shared?
  std::optional<std::vector<int64_t>> shape; // array dims, if known
};

struct Output {
  std::string name;
  std::string dtype;
  std::string reveal_to;                   // "A" | "B" | "both" | "none"
};

struct RevealOutput {
  std::string name;
  std::string reveal_to;                   // "A" | "B" | "both"
  std::string dtype;
  std::string derivation;                  // free-text (excluded from hash)
};

struct Leak {
  std::string kind;                        // e.g. "cardinality", "membership_bit"
  std::string to;                          // "A" | "B" | "both" | "none"
  std::optional<std::string> description;  // free-text (excluded from hash)
};

struct RevealView {
  std::vector<RevealOutput> outputs;
  std::vector<Leak> leaks;
  std::optional<std::string> summary;      // free-text (excluded from hash)
};

struct Cost {                              // informational; never gates consensus
  std::optional<int64_t> gate_count;
  std::optional<int64_t> depth;
  std::optional<int64_t> est_rounds;
};

struct Program {
  std::string source_hash;                 // hash(source + metadata) - input anchor
  std::string toolchain_ver;               // from toolchain.lock
  std::optional<std::string> name;
};

struct Manifest {
  std::string manifest_version = "1.0.0";
  Program program;
  std::vector<std::string> parties{"A", "B"};
  std::vector<Input> inputs;
  std::vector<Output> outputs;
  RevealView reveal_view;
  std::optional<Cost> cost;
  std::optional<std::string> manifest_hash;

  // The structured subset manifest_hash is computed over (see canonical.py
  // semantic_view). Excludes prose, cost, program anchors, and the hash itself.
  json::Value semanticView() const;

  // sha256 hex of the canonical semantic view - the cross-org consent anchor.
  std::string computeHash() const;

  // Full manifest as JSON (for writing manifest.json). Sets manifest_hash.
  json::Value toJson() const;

  // Convenience: set manifest_hash to computeHash().
  void setHash() { manifest_hash = computeHash(); }

  // Fail-closed validation against schema v1 (enum domains, required fields,
  // I/O ↔ reveal-view consistency). Returns empty string if valid, else the
  // first error. The emit pass refuses to write an invalid manifest.
  std::string validate() const;

  // Parse a full manifest (for tests/tooling). Throws on malformed JSON.
  static Manifest fromJson(const json::Value &v);
};

} // namespace ap3

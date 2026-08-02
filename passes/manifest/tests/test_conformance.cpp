// Cross-language conformance: the C++ manifest core must reproduce every golden
// vector from ap3-manifest byte-for-byte - both the canonical semantic string
// and the expected manifest_hash. This is the gate that guarantees a manifest
// emitted by Smaug (C++) hashes identically to one produced by ap3-manifest
// (Python), which is what makes cross-org consensus sound.
//
// Usage: test_conformance <path-to-golden_vectors.json>
#include "ap3/CanonicalJson.h"
#include "ap3/Manifest.h"
#include "test_util.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace ap3;
using namespace ap3test;

static std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "cannot open golden vectors: %s\n", path);
    std::exit(2);
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "golden_vectors.json";
  json::Value doc = json::parse(slurp(path));

  const auto &vectors = doc.at("vectors").items();
  std::printf("conformance: %zu golden vector(s) from %s\n", vectors.size(), path);

  for (const auto &vec : vectors) {
    std::string name = vec.at("name").asStr();
    Manifest m = Manifest::fromJson(vec.at("manifest"));

    // 1. Canonical semantic bytes must match exactly.
    check_eq(m.semanticView().dump(), vec.at("canonical_semantic").asStr(),
             name + ": canonical_semantic");
    // 2. manifest_hash must match exactly.
    check_eq(m.computeHash(), vec.at("expected_manifest_hash").asStr(),
             name + ": manifest_hash");
    // 3. Every golden manifest is schema-valid by our validator.
    check(m.validate().empty(), name + ": validate()");
  }

  return report("conformance");
}

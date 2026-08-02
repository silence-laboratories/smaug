// Validates the metadata -> manifest builder against a Python-computed reference.
// Parses the real benchmarks/metadata/psi.ll.json, builds the AP3 manifest, and
// asserts the canonical semantic bytes and manifest_hash match what ap3-manifest
// (Python) produces for the same logical manifest. This extends the golden-vector
// conformance to a manifest Smaug actually emits (not just hand-authored fixtures).
//
// Usage: test_ap3_metadata <path-to-psi.ll.json>
#include "ap3/Ap3Metadata.h"
#include "ap3/Manifest.h"
#include "test_util.h"

#include <fstream>
#include <sstream>

using namespace ap3;
using namespace ap3test;

// Reference values from ap3-manifest (Python) for the psi manifest - the
// integrity anchors (source_hash/toolchain_ver) are excluded from the hash, so
// any placeholder is fine here.
static const char *EXPECTED_CANON =
    "{\"inputs\":[{\"dtype\":\"i32[]\",\"name\":\"X\",\"party\":\"A\",\"private\":true,\"shape\":null},"
    "{\"dtype\":\"i32[]\",\"name\":\"Y\",\"party\":\"B\",\"private\":true,\"shape\":null},"
    "{\"dtype\":\"i32\",\"name\":\"N\",\"party\":\"public\",\"private\":false,\"shape\":null}],"
    "\"manifest_version\":\"1.0.0\","
    "\"outputs\":[{\"dtype\":\"i32[]\",\"name\":\"intersection\",\"reveal_to\":\"both\"}],"
    "\"parties\":[\"A\",\"B\"],"
    "\"reveal_view\":{\"leaks\":[{\"kind\":\"intersection_elements\",\"to\":\"both\"}],"
    "\"outputs\":[{\"dtype\":\"i32[]\",\"name\":\"intersection\",\"reveal_to\":\"both\"}]}}";
static const char *EXPECTED_HASH =
    "2ce1e7a250d66910b3f8d303c70f991aef414ea0cbb7ec5add1c540c0d813b7b";

static std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "cannot open metadata: %s\n", path);
    std::exit(2);
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "psi.ll.json";
  auto specs = parseAp3Metadata(slurp(path));
  check(specs.count("_Z3psiPiS_iS_") == 1, "psi function has an ap3 block");

  std::string err;
  auto entry = selectEntry(specs, err);
  check(entry.has_value(), "selectEntry found the entry: " + err);
  if (!entry)
    return report("ap3_metadata");

  check_eq(entry->program, "psi", "program name");
  check(entry->inputs.size() == 3, "3 inputs parsed");
  check(entry->outputs.size() == 1, "1 output parsed");
  check_eq(entry->inputs[0].party, "A", "X owned by A");
  check_eq(entry->inputs[1].party, "B", "Y owned by B");
  check_eq(entry->inputs[2].party, "public", "N is public");
  check_eq(entry->outputs[0].reveal_to, "both", "intersection revealed to both");

  Manifest m = buildManifest(*entry, std::string(64, '0'), "test");
  check_eq(m.validate(), "", "built manifest is schema-valid");
  check_eq(m.semanticView().dump(), EXPECTED_CANON, "canonical matches Python");
  check_eq(m.computeHash(), EXPECTED_HASH, "manifest_hash matches Python");

  return report("ap3_metadata");
}

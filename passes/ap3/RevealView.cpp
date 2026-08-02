#include "RevealView.h"

#include "ap3/Ap3Metadata.h"
#include "ap3/CanonicalJson.h"
#include "ap3/Manifest.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>

using namespace llvm;

// AP3-namespaced options so they never collide with the pipeline's own
// --metadata-path (defined in Metadata.cpp) when both live in libpasses.so.
static cl::opt<std::string>
    Ap3MetadataPath("ap3-metadata-path",
                    cl::desc("Path to the metadata JSON carrying \"ap3\" blocks. "
                             "Empty disables manifest emission."),
                    cl::value_desc("path"), cl::init(""));
static cl::opt<std::string>
    Ap3ManifestOut("ap3-manifest-out",
                   cl::desc("Where to write the emitted manifest.json "
                            "(default: <module>.manifest.json)."),
                   cl::value_desc("path"), cl::init(""));
static cl::opt<std::string>
    Ap3SourceHash("ap3-source-hash",
                  cl::desc("Input anchor: hash(source+metadata). "
                           "Supplied by the pod; defaults to 64 zeros."),
                  cl::value_desc("hex"), cl::init(""));
static cl::opt<std::string>
    Ap3ToolchainVer("ap3-toolchain-ver",
                    cl::desc("toolchain_ver string. Empty => read from the "
                             "toolchain lock, else \"smaug-dev\"."),
                    cl::value_desc("str"), cl::init(""));
static cl::opt<std::string>
    Ap3ToolchainLock("ap3-toolchain-lock",
                     cl::desc("Path to toolchain.lock for toolchain_ver."),
                     cl::value_desc("path"), cl::init("/opt/smaug/toolchain.lock"));

namespace {

// Read a whole file into a string; returns false if it cannot be opened.
bool readFile(const std::string &path, std::string &out) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> buf = MemoryBuffer::getFile(path);
  if (!buf)
    return false;
  out = (*buf)->getBuffer().str();
  return true;
}

// Resolve toolchain_ver: explicit flag > toolchain.lock's field > "smaug-dev".
std::string resolveToolchainVer() {
  if (!Ap3ToolchainVer.empty())
    return Ap3ToolchainVer;
  std::string lock;
  if (readFile(Ap3ToolchainLock, lock)) {
    try {
      ap3::json::Value v = ap3::json::parse(lock);
      if (v.has("toolchain_ver"))
        return v.at("toolchain_ver").asStr();
    } catch (const std::exception &) {
      // fall through to the default
    }
  }
  return "smaug-dev";
}

// Best-effort dtype inference for a scalar integer argument when the metadata
// leaves dtype empty. Array/pointer dtypes must be declared in metadata: LLVM-18
// opaque pointers carry no element type, so the compiler cannot recover it.
std::string inferScalarDtype(const Function *F, int argIdx) {
  if (!F || argIdx < 0 || argIdx >= (int)F->arg_size())
    return "";
  Type *t = F->getArg(argIdx)->getType();
  if (auto *it = dyn_cast<IntegerType>(t))
    return "i" + std::to_string(it->getBitWidth());
  return "";
}

// Fill in dtypes the developer left to inference, and structurally cross-check
// the spec against the function. Returns an error string (fail-closed) or "".
std::string enrichAndCheck(ap3::Ap3FunctionSpec &spec, const Function *F) {
  const unsigned nargs = F ? F->arg_size() : 0;
  auto checkArg = [&](int arg, const std::string &who) -> std::string {
    if (F && arg >= 0 && (unsigned)arg >= nargs)
      return who + " references arg " + std::to_string(arg) + " but '" +
             spec.func_name + "' has " + std::to_string(nargs) + " args";
    return "";
  };

  for (auto &in : spec.inputs) {
    if (std::string e = checkArg(in.arg, "input '" + in.name + "'"); !e.empty())
      return e;
    if (in.dtype.empty())
      in.dtype = inferScalarDtype(F, in.arg);
    if (in.dtype.empty())
      return "input '" + in.name + "' has no dtype and it cannot be inferred "
             "(declare dtype in the ap3 metadata for pointer/array inputs)";
  }
  for (auto &out : spec.outputs) {
    if (std::string e = checkArg(out.arg, "output '" + out.name + "'"); !e.empty())
      return e;
    if (out.dtype.empty())
      out.dtype = inferScalarDtype(F, out.arg);
    if (out.dtype.empty())
      return "output '" + out.name + "' has no dtype and it cannot be inferred";
  }
  return "";
}

} // namespace

namespace ap3 {

PreservedAnalyses RevealViewPass::run(Module &M, ModuleAnalysisManager &) {
  // No metadata path => emission disabled; the pass is a safe no-op in the
  // pipeline for programs that carry no ap3 annotations.
  if (Ap3MetadataPath.empty())
    return PreservedAnalyses::all();

  std::string text;
  if (!readFile(Ap3MetadataPath, text)) {
    report_fatal_error(Twine("ap3-reveal-view: cannot read metadata: ") +
                       Ap3MetadataPath);
  }

  std::map<std::string, Ap3FunctionSpec> specs;
  try {
    specs = parseAp3Metadata(text);
  } catch (const std::exception &e) {
    report_fatal_error(Twine("ap3-reveal-view: bad ap3 metadata: ") + e.what());
  }
  if (specs.empty()) {
    errs() << "ap3-reveal-view: no \"ap3\" annotations found; nothing to emit\n";
    return PreservedAnalyses::all();
  }

  std::string err;
  auto entry = selectEntry(specs, err);
  if (!entry)
    report_fatal_error(Twine("ap3-reveal-view: ") + err);

  // The mangled entry function should survive the pipeline; warn if not so the
  // developer notices a name mismatch, but continue (dtypes may be declared).
  Function *F = M.getFunction(entry->func_name);
  if (!F)
    errs() << "ap3-reveal-view: warning: entry function '" << entry->func_name
           << "' not found in module; relying on declared dtypes\n";

  if (std::string e = enrichAndCheck(*entry, F); !e.empty())
    report_fatal_error(Twine("ap3-reveal-view: ") + e);

  std::string sourceHash = Ap3SourceHash.empty() ? std::string(64, '0') : Ap3SourceHash;
  Manifest m = buildManifest(*entry, sourceHash, resolveToolchainVer());

  // Fail-closed: never write an invalid manifest.
  if (std::string v = m.validate(); !v.empty())
    report_fatal_error(Twine("ap3-reveal-view: invalid manifest: ") + v);

  std::string out = Ap3ManifestOut;
  if (out.empty())
    out = (sys::path::filename(M.getName()) + ".manifest.json").str();

  std::string body = m.toJson().dump(); // canonical, deterministic bytes
  std::ofstream ofs(out, std::ios::binary | std::ios::trunc);
  if (!ofs)
    report_fatal_error(Twine("ap3-reveal-view: cannot write manifest: ") + out);
  ofs << body;
  ofs.close();

  errs() << "ap3-reveal-view: wrote " << out << " (manifest_hash="
         << (m.manifest_hash ? *m.manifest_hash : "?") << ")\n";
  return PreservedAnalyses::all();
}

bool registerAp3RevealView(StringRef Name, ModulePassManager &MPM,
                           ArrayRef<PassBuilder::PipelineElement>) {
  if (Name != "ap3-reveal-view")
    return false;
  MPM.addPass(RevealViewPass());
  return true;
}

} // namespace ap3

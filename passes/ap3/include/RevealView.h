// RevealView - the AP3 manifest-emit LLVM pass.
//
// Smaug EMITS INTO the AP3 manifest: this pass reads the party-aware "ap3"
// annotations from the metadata JSON, cross-checks them against the module, and
// writes a schema-valid manifest.json (fail-closed). Everything downstream -
// policy, consensus, UI, audit - reads that manifest, never the IR.
//
// The pass is a thin adapter over the LLVM-free manifest core (passes/manifest):
// parsing + canonical hashing + validation live there and are unit-tested; this
// file only bridges LLVM (command-line, module lookup, type inference) to it.
#pragma once

#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"

namespace ap3 {

class RevealViewPass : public llvm::PassInfoMixin<RevealViewPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
  static bool isRequired() { return true; }
};

// Pipeline-parsing callback so the pass is available as `ap3-reveal-view` both in
// the standalone plugin and inside libpasses.so (registered from Passes.cpp).
bool registerAp3RevealView(llvm::StringRef Name, llvm::ModulePassManager &MPM,
                           llvm::ArrayRef<llvm::PassBuilder::PipelineElement>);

} // namespace ap3

// Standalone opt plugin exposing just the AP3 manifest-emit pass, so it can be
// built and tested against a stock LLVM-18 without the full Smaug plugin (which
// needs the vendored vectorizer and far more memory). The same RevealView.cpp is
// also compiled into libpasses.so and slotted into smaug-pipeline (see Passes.cpp).
//
//   opt -load-pass-plugin=libap3pass.so -passes=ap3-reveal-view \
//       --ap3-metadata-path=foo.ll.json foo.ll -o /dev/null
#include "RevealView.h"

#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "ap3", LLVM_VERSION_STRING, [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(ap3::registerAp3RevealView);
          }};
}

// LoopUnrollMultiAccum.h
#ifndef LLVM_TRANSFORMS_SCALAR_LOOPUNROLLMULTIACCUM_H
#define LLVM_TRANSFORMS_SCALAR_LOOPUNROLLMULTIACCUM_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Loop;
class LoopStandardAnalysisResults;
class LPMUpdater;

struct LoopUnrollMultiAccumOptions {
// placeholder for LoopUnrollMAOptions
  int NumAccum;
  //int OptLevel;

LoopUnrollMultiAccumOptions(int NumAccum = 4)
    NumAccum(NumAccum) {}
};

class LoopUnrollMultiAccum : public PassInfoMixin<LoopUnrollMultiAccum> {
  LoopUnrollMultiAccumOptions UnrollMultiAccumOpts;

public:

  explicit LoopUnrollMultiAccum(LoopUnrollMultiAccumOptions UnrollMultiAccumOpts = {})
      : UnrollMultiAccumOpts(UnrollMultiAccumOpts) {}

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // namespace llvm

#endif
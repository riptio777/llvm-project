// LoopUnrollMultiAccum.h
#ifndef LLVM_TRANSFORMS_SCALAR_LOOPUNROLLMULTIACCUM_H
#define LLVM_TRANSFORMS_SCALAR_LOOPUNROLLMULTIACCUM_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Loop;
class LoopStandardAnalysisResults;
class LPMUpdater;

struct LoopUnrollMultiAccum : public PassInfoMixin<LoopUnrollMultiAccum> {
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // namespace llvm

#endif
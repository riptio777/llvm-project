// LoopUnrollMultiAccum.h
#ifndef LLVM_TRANSFORMS_SCALAR_LOOPUNROLLMULTIACCUM_H
#define LLVM_TRANSFORMS_SCALAR_LOOPUNROLLMULTIACCUM_H

#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
//#include "llvm/Support/CommandLine.h"

namespace llvm {

class Loop;
struct LoopStandardAnalysisResults;
class LPMUpdater;

struct LoopUnrollMultiAccumOptions {
// placeholder for LoopUnrollMAOptions
  int NumAccum;
  //int OptLevel;

LoopUnrollMultiAccumOptions(int NumAccum = 4):NumAccum(NumAccum) {}

};

class LoopUnrollMultiAccumPass : public PassInfoMixin<LoopUnrollMultiAccumPass> {
  LoopUnrollMultiAccumOptions UnrollMultiAccumOpts;

public:

  explicit LoopUnrollMultiAccumPass(LoopUnrollMultiAccumOptions UnrollMultiAccumOpts = {})
      : UnrollMultiAccumOpts(UnrollMultiAccumOpts) {}

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

};

} // namespace llvm

#endif
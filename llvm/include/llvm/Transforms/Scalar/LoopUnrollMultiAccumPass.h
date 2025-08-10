// LoopUnrollMultiAccum.h
#ifndef LLVM_TRANSFORMS_SCALAR_LOOPUNROLLMULTIACCUM_H
#define LLVM_TRANSFORMS_SCALAR_LOOPUNROLLMULTIACCUM_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Support/DivisionByConstantInfo.h"
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

  static bool isRequired() { return true; }

private:
  bool shouldUnrollLoop(Loop &L, LoopStandardAnalysisResults &AR);
  unsigned getLoopConstantTripCount(Loop &L);
  bool unrollLoopWithAccum(Loop &L, unsigned UnrollFactor, 
                          LoopStandardAnalysisResults &AR,
                          ScalarEvolution &SE);
  PHINode* findReductionVariable(Loop &L);
  Value *createAccumulators(IRBuilder<> &Builder, PHINode *ReductionPHI, 
                            unsigned UnrollFactor, Loop &L);
  bool unrollTest(Loop &L, unsigned UnrollFactor, 
                  LoopStandardAnalysisResults &AR,
                  ScalarEvolution &SE);
};

} // namespace llvm

#endif
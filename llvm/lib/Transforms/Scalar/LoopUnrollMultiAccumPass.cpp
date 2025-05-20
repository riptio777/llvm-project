// LoopUnrollMultiAccum.cpp
#include "llvm/Transforms/Scalar/LoopUnrollMultiAccum.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

using namespace llvm;

#define DEBUG_TYPE "loop-unroll-multiacc"

STATISTIC(NumUnrolledLoops, "Number of loops unrolled with multi-accumulator");

// 新Pass接口继承自PassInfoMixin
struct LoopUnrollMultiAccum : public PassInfoMixin<LoopUnrollMultiAccum> {
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U) {
    // 合法性检查
    if (!L.isLoopSimplifyForm() || !L.hasDedicatedExits())
      return PreservedAnalyses::all();
    
    ScalarEvolution &SE = AR.SE;
    if (!SE.hasLoopInvariantBackedgeTakenCount(&L))
      return PreservedAnalyses::all();

    // 检测可展开的累加循环
    if (!detectAccumulatorLoop(L))
      return PreservedAnalyses::all();

    // 设置展开因子（示例值，可扩展为自动计算）
    unsigned UnrollFactor = 4;
    bool AllowPartial = false;

    // 使用新版循环展开接口
    UnrollLoopOptions ULO;
    ULO.Count = UnrollFactor;
    ULO.AllowPartial = AllowPartial;
    ULO.ForgetAllSCEV = true;

    if (UnrollLoop(&L, ULO, &AR.LI, &SE, &AR.DT, &AR.AC, &AR.TTI, &AR.CG, true)) {
      // 展开成功后插入多累加器优化
      splitAccumulators(L);
      NumUnrolledLoops++;
      return PreservedAnalyses::none();
    }
    return PreservedAnalyses::all();
  }

private:
  bool detectAccumulatorLoop(Loop &L) {
    // 检测包含单一累加变量的循环
    for (auto *BB : L.blocks()) {
      for (auto &I : *BB) {
        if (auto *Phi = dyn_cast<PHINode>(&I)) {
          if (isAccumulatorPhi(Phi))
            return true;
        }
      }
    }
    return false;
  }

  void splitAccumulators(Loop &L) {
    // 创建多个PHI节点并替换原有累加逻辑
    for (auto *BB : L.blocks()) {
      for (auto &I : *BB) {
        if (auto *Phi = dyn_cast<PHINode>(&I)) {
          if (isAccumulatorPhi(Phi)) {
            SmallVector<PHINode*, 4> NewPhis;
            createMultiAccumPhis(Phi, NewPhis);
            replaceAccumulatorUses(Phi, NewPhis);
          }
        }
      }
    }
  }

  // 具体实现细节参考LLVM官方LoopUnrollPass源码
};

// Pass注册插件接口（LLVM 18新特性）
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "LoopUnrollMultiAccum", "v0.1",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
          [](StringRef Name, LoopPassManager &LPM,
             ArrayRef<PassBuilder::PipelineElement>) {
            if (Name == "loop-unroll-multiacc") {
              LPM.addPass(LoopUnrollMultiAccum());
              return true;
            }
            return false;
          });
    }
  };
}
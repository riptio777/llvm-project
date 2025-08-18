#ifndef LLVM_TRANSFORMS_SCALAR_BRTOSELECT_H
#define LLVM_TRANSFORMS_SCALAR_BRTOSELECT_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class BrToSelectPass : public PassInfoMixin<BrToSelectPass> {
    
public:

    //BrToSelectPass();

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);


};

}

#endif
// LoopUnrollMultiAccum.cpp
#include "llvm/Transforms/Scalar/LoopUnrollMultiAccumPass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cassert>
#include <cstdint>
#include <vector>


using namespace llvm;

#define DEBUG_TYPE "loop-unroll-multiacc"
#define UNROLL_FACTOR 2

STATISTIC(NumUnrolledLoops, "Number of loops unrolled with multi-accumulator");

PreservedAnalyses LoopUnrollMultiAccumPass::run(
    Loop &L, LoopAnalysisManager &AM, LoopStandardAnalysisResults &AR,
    LPMUpdater &Updater) {
    
  errs() << "Header name: " << L.getHeader()->getName() << "\n";
  errs() << "Loop preheader: " << L.getLoopPreheader()->getName() << "\n";
  errs() << "Loop latch: " << L.getLoopLatch()->getName() << "\n";
    
  if (!L.isLoopSimplifyForm() || !L.isLCSSAForm(AR.DT)) {
    return PreservedAnalyses::all();
  }

  if (L.isRotatedForm()) {
    errs() << "Loop is in rotated form!" << "\n";
  } else {
    errs() << "Loop is NOT in rotated form!" << "\n";
  }

  if (!shouldUnrollLoop(L, AR)) { return PreservedAnalyses::all(); }


  auto &DT = AR.DT; // Get Dominant Tree
  ScalarEvolution &SE = AR.SE;

  errs() << "=============== start of testing output ================" << "\n";
  PHINode *ReductionPHI = findReductionVariable(L);

  // IR builder for multi-accumulators
  Instruction* headerTerminator = L.getHeader()->getTerminator();
  IRBuilder<> Builder(L.getLoopPreheader()->getTerminator());
  errs() << "Header terminator: " << *headerTerminator << "\n";
  unrollTest(L, UNROLL_FACTOR, AR, SE);

  //Value *AccumArray = createAccumulators(Builder, ReductionPHI, UNROLL_FACTOR, L);
  
  //errs() << "AccumArray: " << *AccumArray << "\n";

  errs() << "=============== end of testing output ================" << "\n";

/*
  for (BasicBlock *BB : L.getBlocks()) {
    if (L.isLoopExiting(BB)) {
      errs() << "Exiting Block: " << BB->getName() << "\n"; 
      // Perform some analysis or transformation on the block
      unsigned TC = SE.getSmallConstantTripCount(&L, BB);
      errs() << "TripCount thru existing block: " << TC << "\n";  
    }
    
    if (DT.dominates(L.getHeader(), BB)) {
      errs() << BB->getName() << " is dominated by Header\n";
    }
  }

  auto* BECount = SE.getBackedgeTakenCount(&L);
  if(auto* BTCCont = dyn_cast<SCEVConstant>(BECount)) {
    uint64_t BTCValue = BTCCont->getValue()->getZExtValue();
    errs() << "Constant BTC: " << BTCValue << "\n";
  }
*/
  return PreservedAnalyses::all(); // Placeholder for actual implementation
}

// TODO: probably AR is not needed here
bool LoopUnrollMultiAccumPass::shouldUnrollLoop(Loop &L, LoopStandardAnalysisResults &AR) {
  if (!L.isRotatedForm()) { return false; }

  // Need to have constant trip count
  if (getLoopConstantTripCount(L) == 0) { return false; }

  // TODO: remove, for debugging purposes
  errs() << "getLoopConstantTripCount : " << getLoopConstantTripCount(L) << "\n";

  return true;
}

unsigned LoopUnrollMultiAccumPass::getLoopConstantTripCount(Loop &L) {
  unsigned TripCount = 0;
  if (auto *ExitCond = dyn_cast<ICmpInst>(L.getLoopLatch()
  ->getTerminator()->getOperand(0))) {
      //errs() << "calling Bound->getZExtValue()" << "\n";

    if (auto *Bound = dyn_cast<ConstantInt>(ExitCond->getOperand(1))) {
      TripCount = Bound->getZExtValue();
    }
  }
  return TripCount; 
}

Value *LoopUnrollMultiAccumPass::createAccumulators(IRBuilder<> &Builder, 
  PHINode *ReductionPHI, unsigned UnrollFactor, Loop &L) {
    // Create accumulator array
    Type *ReductionTy = ReductionPHI->getType();
    AllocaInst *AccumAllocaInst = Builder.CreateAlloca(
      ReductionTy,
      ConstantInt::get(Builder.getInt32Ty(), UnrollFactor), 
      "accums"
    );

    // initialize accumulators
    Value *InitVal = ReductionPHI->getIncomingValueForBlock(L.getLoopPreheader());
    assert(InitVal && "Failed to get initial value for reduction var");
    errs() << "Reduction initial value: " << *InitVal << "\n";
    for (unsigned i = 0; i < UnrollFactor; i++) {
      Value *GEP = Builder.CreateInBoundsGEP(
          AccumAllocaInst->getAllocatedType(),
          AccumAllocaInst,
          {Builder.getInt32(i)}
      );
      Builder.CreateStore(InitVal, GEP);
      errs() << "GEP created: " << *GEP << "\n";
    }

    return AccumAllocaInst;
}

bool LoopUnrollMultiAccumPass::unrollTest(Loop &L, unsigned UnrollFactor, 
  LoopStandardAnalysisResults &AR, ScalarEvolution &SE) {
  LoopInfo &LI = AR.LI;
  DominatorTree &DT = AR.DT;
  
  // get loop header and latch
  BasicBlock *Header = L.getHeader();
  BasicBlock *Latch = L.getLoopLatch();

  PHINode *IV = L.getCanonicalInductionVariable();
    if (!IV) { errs() << "no canonical IV" << "\n"; }
    else { errs() << "canonical IV: " << *IV << "\n"; }
  
    
  SmallVector<BasicBlock*> UnrolledBlocks;
  ValueToValueMapTy VMapArray;
  SmallVector<BasicBlock*, 1> ClonedBBs;
  Function *Func = Header->getParent();

    ValueToValueMapTy VMap;

    BasicBlock *NewBB = CloneBasicBlock(
        Header,
        VMap,
        ".unrolled." + Twine(0)
    );

  
    
    errs() << "FUNC..." << "\n";
    //Func->viewCFG();
    //Func->dump();
  

  IRBuilder<> Builder(L.getLoopPreheader()->getTerminator());
  Func->insert(std::next(Header->getIterator()), NewBB);
  BranchInst *BI = dyn_cast<BranchInst>(Header->getTerminator());
  BI->setSuccessor(0, NewBB);

  // update cloned bb's phi node
  // From %sum.1.unrolled.0 = phi i32 [ 5, %for.body.lr.ph ], [ %add, %for.inc ]
  // to %sum.1.unrolled.0 = phi i32 [ 5, %for.body.lr.ph ], [ %add, %for.body ]
  // %for.inc is the Latch
  for(Instruction &I : *NewBB) {
    if (PHINode *PN = dyn_cast<PHINode>(&I)) {
      for (unsigned i = 0; i < PN->getNumIncomingValues(); ++i) {
        if (PN->getIncomingBlock(i) == Latch) {
          Value *OriginalVal = PN->getIncomingValue(i);
          PN->setIncomingBlock(i, Header);
        }
      }
    }
  }

  // change step size
  Value *Step = ConstantInt::get(IV->getType(), UnrollFactor);
  //IV->addIncoming(Value *V, BasicBlock *BB)
  Func->dump();

  return true;
}

bool LoopUnrollMultiAccumPass::unrollLoopWithAccum(Loop &L, unsigned UnrollFactor, 
  LoopStandardAnalysisResults &AR, ScalarEvolution &SE) {
  LoopInfo &LI = AR.LI;
  DominatorTree &DT = AR.DT;
  
  // get loop header and latch
  BasicBlock *Header = L.getHeader();
  BasicBlock *Latch = L.getLoopLatch();

  PHINode *IV = L.getCanonicalInductionVariable();
    if (!IV) { errs() << "no canonical IV" << "\n"; }
    else { errs() << "canonical IV: " << *IV << "\n"; }
  
    
  SmallVector<BasicBlock*> UnrolledBlocks;
  ValueToValueMapTy VMapArray;
  SmallVector<BasicBlock*, 1> ClonedBBs;
  Function *Func = Header->getParent();

  for (unsigned i = 0; i < UnrollFactor; i++) {
    ValueToValueMapTy VMap;

    BasicBlock *NewBB = CloneBasicBlock(
        Header,
        VMap,
        ".unrolled." + Twine(i)
    );

    
    errs() << "FUNC..." << "\n";
    Func->viewCFG();
    //Func->dump();
    if (i == 0) {
      // insert cloned basic block after original for.body block
     // auto BlockInsertPt = std::next(Header->getIterator());
     // Func->insert(BlockInsertPt, NewBB);
    }
    /*
    for (ValueToValueMapTy::iterator VI = VMap.begin(), VE = VMap.end();
         VI != VE; ++VI) {
          //errs() << "VMap value: " << *VI->first << " " << *VI->second << "\n";
    }
    
    for(auto &Inst : *NewBB) {
      RemapInstruction(&Inst, VMap,RF_IgnoreMissingLocals);
    }
      */
      
   // ClonedBBs = {NewBB};
    // Remap instructions
    //remapInstructionsInBlocks(ClonedBBs, VMap);

 

    UnrolledBlocks.push_back(NewBB);
    assert(NewBB && "Failed to create a new BasicBlock");

    errs() << "new BB: " << *NewBB << "\n";
  }

  IRBuilder<> Builder(L.getLoopPreheader()->getTerminator());
  
  Func->dump();
  // change step size
  Value *Step = ConstantInt::get(IV->getType(), UnrollFactor);
  //IV->addIncoming(Value *V, BasicBlock *BB)

  return true;
}


PHINode* LoopUnrollMultiAccumPass::findReductionVariable(Loop &L) {
    errs() << "Header: " << L.getHeader()->getName() << "\n";
    for (PHINode& PN : L.getHeader()->phis()) {
      auto* p = PN.getIncomingValue(0);
      auto* p1 = PN.getIncomingValue(1);
      errs() << "p->getType(): " << *(p->getType()) << "\n";
      Instruction *I = dyn_cast<Instruction>(p1);
      //Instruction *I = dyn_cast<Instruction>(p); 
      // above returns null, because p is not Instruction

      if (I) {
        if (I->getOpcode() == Instruction::Add || 
            I->getOpcode() == Instruction::FAdd) {
          errs() << "updated through add: " << PN << "\n";
          errs() << "reduction instruction: " << *I << "\n";
          auto op1 = I->getOperand(1);
          if (auto* incr = dyn_cast<ConstantInt>(op1)) {
            errs() << "constant increment... " << *incr << "\n";
          }
          return &PN;
        }
      }

      //if (auto* BinOp = dyn_cast<BinaryOperator()))
    }
}



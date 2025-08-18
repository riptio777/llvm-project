#include "llvm/Transforms/Scalar/BrToSelect.hh"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "br2select"

static bool runPass(Function &F) {
}

PreservedAnalyses BrToSelectPass::run(Function &F, FunctionAnalysisManager &AM) {
    bool Changed = false;

    SmallVector<BranchInst*, 8> Candidates;
    // select the candidate instructions to process
    for (BasicBlock &BB : F) {
        Instruction *T = BB.getTerminator();
        if (!T) continue;
        
        // check if the terminator is an unconditional branch
        if (auto *BI = dyn_cast<BranchInst>(T)) {
            if (BI->isConditional()) {
                Candidates.push_back(BI);
            }
        }
    }

    for (BranchInst *BI : Candidates) {
        BasicBlock *BB = BI->getParent();
        BasicBlock *TBB = BI->getSuccessor(0);
        BasicBlock *FBB = BI->getSuccessor(1);

        // Both the true and false blocks have to unconditionally
        // branch to the same merge block
        // first make sure that the two BBs have a terminating instruction
        Instruction *TBI = TBB->getTerminator();
        Instruction *FBI = FBB->getTerminator();
        if (!TBI || !FBI) { continue; }
        BranchInst *TBterm = dyn_cast<BranchInst>(TBI);
        BranchInst *FBterm = dyn_cast<BranchInst>(FBI);
        if (!TBterm || !FBterm) { continue; }
        // Both have to be unconditional
        if (TBterm->isConditional() || FBterm->isConditional()) { continue; }
        BasicBlock *TrueMergeBB = TBterm->getSuccessor(0);
        BasicBlock *FalseMergeBB = FBterm->getSuccessor(0);

        // Both have to branch to the same merge block
        if (TrueMergeBB != FalseMergeBB) {
            continue;
        }

        BasicBlock *MergeBB = TrueMergeBB;

        // Collect the phi nodes of the merge block
        SmallVector<PHINode*, 4> PHIs;
        for (Instruction &I : *MergeBB) {
            if (auto *PN = dyn_cast<PHINode>(&I)) {
                PHIs.push_back(PN);
            }
            else {
                break;
            }
        }
        if (PHIs.empty()) { continue; }

        // Find a PHI node that selects between TBB and FBB
        PHINode *TargetPHI = nullptr;
        for (PHINode *PN : PHIs) {
            // First, this PHI node should have 2 incoming values
            if (PN->getNumIncomingValues() != 2) { continue; }
            BasicBlock *B0 = PN->getIncomingBlock(0);
            BasicBlock *B1 = PN->getIncomingBlock(1);
            if ((B0 == TBB && B1 == FBB) || (B0 == FBB && B1 == TBB)) {
                TargetPHI = PN;
                break;
            }
        }

        if (!TargetPHI) { continue; }

        Value *TrueVal = TargetPHI->getIncomingValueForBlock(TBB);
        Value *FalseVal = TargetPHI->getIncomingValueForBlock(FBB);

        // Decide if it's safe to use the values in a select 
        auto isSafeValueInBlock  = [&](Value *V, BasicBlock *Src) -> bool {
            if (auto *I = dyn_cast<Instruction>(V)) {
                // Must be defined in the source bb
                if (I->getParent() != Src) { return false; }
                // Must not have any side effects
                if (I->mayHaveSideEffects()) { return false; }

                // Must be a non-PHI instruction and not the terminator
                if (isa<PHINode>(I) || I->isTerminator()) { return false; }
                
                // Check if there are other instructions besides
                // debug instrinsics after I
                for (auto it = std::next(I->getIterator()); it != Src->end(); ++it) {
                    if (&*it == Src->getTerminator()) { break; }
                    if (isa<DbgInfoIntrinsic>(&*it)) { continue; }
                    return false;
                }
                // Simple instruction
                return true;
            }

            return true;
        };

        // Make sure that the TrueVal and FalseVal are safe to be moved to Merge Block
        if (!isSafeValueInBlock(TrueVal, TBB) || !isSafeValueInBlock(FalseVal, FBB)) {
            continue;
        }

        
        IRBuilder<> Builder(&*MergeBB->getFirstInsertionPt());

        // Create the select instruction
        Value *Cond = BI->getCondition();
        Value *Sel = Builder.CreateSelect(Cond, TrueVal, FalseVal, 
                                    TargetPHI->getName() + ".sel");
    

        // Insert TrueVal and FalseVal into Merge Block, after the target PHI node
        Instruction *TrueValInst = dyn_cast<Instruction>(TrueVal);
        if (TrueValInst) {
            TrueValInst->moveBefore(dyn_cast<Instruction>(Sel)->getIterator());
        }
        Instruction *FalseValInst = dyn_cast<Instruction>(FalseVal);
        if (FalseValInst) {
            FalseValInst->moveBefore(dyn_cast<Instruction>(Sel)->getIterator());
        }

        // Replace PHI uses with select
        TargetPHI->replaceAllUsesWith(Sel);

        // Delete the replaced PHI from parent
        TargetPHI->eraseFromParent();
        
        //errs() << MergeBB << "\n";
        // Determine if we could remove the True and False blocks
        // They should be dead actually at this point
        // Otherwise it wouldn't have been able to be replaced
        // First create an unconditional branch from BB to MergeBB
        // Replace the conditional br with the new br
        BranchInst *NewBr = BranchInst::Create(MergeBB, BI->getIterator());
        BI->eraseFromParent();

        if (pred_empty(TBB)) {
            DeleteDeadBlock(TBB);
        }
        if (pred_empty(FBB)) {
            DeleteDeadBlock(FBB);
        }


        Changed = true;

    } // End of outer for loop
    F.print(errs());
    return (Changed ? PreservedAnalyses::none() : PreservedAnalyses::all());
}
//===- NopInjectPass.cpp - Inject x86 NOP before branches ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/NopInjectPass.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// Returns true for every instruction that represents a branch or indirect
/// transfer of control.
/// To restrict to conditional branches only, replace with:
///   if (auto *BI = dyn_cast<BranchInst>(&I)) return BI->isConditional();
static bool isBranchLike(const Instruction &I) {
  return isa<BranchInst>(I)     // br / br cond
      || isa<SwitchInst>(I)     // switch
      || isa<IndirectBrInst>(I) // indirectbr
      || isa<CallInst>(I)       // call / tail-call
      || isa<InvokeInst>(I);    // invoke (call that may unwind)
}

/// Inserts  call void asm sideeffect "nop", "~{dirflag},~{fpsr},~{flags}"()
/// immediately before \p I.
static void insertNopBefore(Instruction &I) {
  LLVMContext &Ctx = I.getContext();
  FunctionType *AsmFTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  InlineAsm *NopAsm = InlineAsm::get(
      AsmFTy,
      "nop",
      "~{dirflag},~{fpsr},~{flags}",
      /*hasSideEffects=*/true,
      /*isAlignStack=*/false,
      InlineAsm::AD_ATT);
  IRBuilder<> Builder(&I);
  Builder.CreateCall(NopAsm, {});
}

/// Walks every basic block in \p F and inserts a NOP before each branch-like
/// instruction. Returns true iff the function was modified.
static bool injectNops(Function &F) {
  bool Changed = false;
  for (BasicBlock &BB : F) {
    // Snapshot targets first to avoid invalidating the iterator.
    SmallVector<Instruction *, 8> Targets;
    for (Instruction &I : BB)
      if (isBranchLike(I))
        Targets.push_back(&I);
    for (Instruction *I : Targets) {
      insertNopBefore(*I);
      Changed = true;
    }
  }
  return Changed;
}

// ─── New Pass Manager ────────────────────────────────────────────────────────

PreservedAnalyses NopInjectPass::run(Function &F, FunctionAnalysisManager &) {
  if (!injectNops(F))
    return PreservedAnalyses::all();

  // We inserted call instructions but did not alter the CFG topology,
  // so all CFG-based analyses remain valid.
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

//===- NopInjectPass.h - Inject x86 NOP before branches --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares NopInjectPass for the new pass manager (LLVM 14+).
///
/// Inserts an x86 "nop" inline-assembly call immediately before every
/// branch-like instruction (BranchInst, SwitchInst, IndirectBrInst,
/// CallInst, InvokeInst) in each function it visits.
///
/// In-tree wiring required:
///   llvm/lib/Transforms/Utils/CMakeLists.txt  — add NopInjectPass.cpp
///   llvm/lib/Passes/PassRegistry.def          — FUNCTION_PASS entry
///   llvm/lib/Passes/PassBuilder.cpp           — #include this header
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_NOPINJECTPASS_H
#define LLVM_TRANSFORMS_UTILS_NOPINJECTPASS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

// class Function;
// class FunctionAnalysisManager;

/// NopInjectPass - New pass manager function pass.
///
/// Inserts an x86 "nop" inline-assembly call before every branch-like
/// instruction. Does not alter the CFG, so all CFG-based analyses are
/// preserved.
///
/// Example IR produced around a conditional branch:
/// \code
///   call void asm sideeffect "nop", "~{dirflag},~{fpsr},~{flags}"()
///   br i1 %cond, label %true_bb, label %false_bb
/// \endcode
///
/// Usage (opt):
///   opt -passes="nop-inject" -S input.ll -o output.ll
///
/// Usage (programmatic):
///   FunctionPassManager FPM;
///   FPM.addPass(NopInjectPass());
class NopInjectPass : public PassInfoMixin<NopInjectPass> {
public:
  /// Run the pass over \p F.
  ///
  /// \returns PreservedAnalyses::all() if no branches were found;
  ///          otherwise preserves CFGAnalyses (the CFG is not modified).
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);

  /// Allow the pass to run on functions with any linkage, including
  /// internal/private (e.g. static functions, lambdas).
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_NOPINJECTPASS_H

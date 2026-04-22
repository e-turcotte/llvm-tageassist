#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static llvm::cl::opt<bool> SkipNopInjection(
    "skip-nop-injection", 
    llvm::cl::init(false), 
    llvm::cl::desc("Skip the custom X86 Branch Nop Injection Pass")
);

struct X86BranchNopInjection : public MachineFunctionPass {
    static char ID;
    X86BranchNopInjection() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &MF) override;
};

static bool
runX86BranchNopInjection(MachineFunction &MF) {
	const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
	bool Modified = false;

	for (MachineBasicBlock &MBB : MF) {
		auto FirstTerm = MBB.getFirstTerminator();

		if (FirstTerm == MBB.end())
			continue;

		if (!FirstTerm->isConditionalBranch() && !FirstTerm->isIndirectBranch())
			continue; // unconditional — no hint needed

		BuildMI(MBB, FirstTerm, FirstTerm->getDebugLoc(),
				TII->get(X86::NOOP));
	}

	return Modified;
}

bool X86BranchNopInjection::runOnMachineFunction(
    MachineFunction &MF) {

  if (SkipNopInjection)
    return false; // Do nothing and indicate no changes were made

  return runX86BranchNopInjection(MF);
}

PreservedAnalyses X86BranchNopInjectionPass::run(
    MachineFunction &MF, MachineFunctionAnalysisManager &MFAM) {
  return runX86BranchNopInjection(MF)
             ? getMachineFunctionPassPreservedAnalyses()
                   .preserveSet<CFGAnalyses>()
             : PreservedAnalyses::all();
}

char X86BranchNopInjection::ID = 0;

FunctionPass *llvm::createX86BranchNopInjectionPass() {
  return new X86BranchNopInjection();
}

INITIALIZE_PASS(X86BranchNopInjection, "x86-branch-nopinjection",
                "X86 Branch Nop Injection", false,
                false)

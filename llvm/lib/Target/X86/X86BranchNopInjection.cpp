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

using namespace llvm;

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
		for (auto I = MBB.begin(); I != MBB.end(); ++I) {
			if (I->isBranch()) {
				// Insert NOP immediately before the branch
				BuildMI(MBB, I, I->getDebugLoc(),
						TII->get(X86::NOOP)); // swap opcode for your ISA
				Modified = true;
			}
		}
	}
	return Modified;
}

bool X86BranchNopInjection::runOnMachineFunction(
    MachineFunction &MF) {
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

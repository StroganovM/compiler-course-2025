#include "X86.h"
#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/ADT/DenseMap.h"

using namespace llvm;

namespace {
class X86LogicOptPass : public MachineFunctionPass {
public:
  static char ID;
  X86LogicOptPass() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    const auto *TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();
    MachineRegisterInfo &MRI = MF.getRegInfo();
    bool Changed = false;

    DenseMap<unsigned, unsigned> AVXOpcodeMap = {
      { X86::PANDrr,   X86::VPANDrr },
      { X86::PORrr,    X86::VPORrr },
      { X86::PXORrr,   X86::VPXORrr },
      { X86::ANDPSrr,  X86::VANDPSrr },
      { X86::ORPSrr,   X86::VORPSrr },
      { X86::XORPSrr,  X86::VXORPSrr }
    };

    auto isLogicOp = [&](unsigned Opc) {
      return AVXOpcodeMap.count(Opc);
    };

    for (MachineBasicBlock &MBB : MF) {
      for (auto MII = MBB.begin(); MII != MBB.end(); ) {
        MachineInstr &MI = *MII;
        unsigned Opc = MI.getOpcode();

        if (Opc == X86::PORrr || Opc == X86::ORPSrr || Opc == X86::VPORrr || Opc == X86::VORPSrr) {
          Register Dest = MI.getOperand(0).getReg();
          Register Src1 = MI.getOperand(1).getReg();
          Register Src2 = MI.getOperand(2).getReg();
          
          if (Src1 == Src2) {
            BuildMI(MBB, MII, MI.getDebugLoc(), TII->get(TargetOpcode::COPY), Dest)
                .addReg(Src1);
            MI.eraseFromParent();
            Changed = true;
            MII = MBB.begin();
            continue;
          }
        }
        
        if (Opc == X86::PANDrr || Opc == X86::ANDPSrr || Opc == X86::VPANDrr || Opc == X86::VANDPSrr) {
          Register Dest = MI.getOperand(0).getReg();
          Register Src1 = MI.getOperand(1).getReg();
          MachineOperand &Src2Op = MI.getOperand(2);
		  
          if (Src2Op.isImm() && Src2Op.getImm() == 0) {
            BuildMI(MBB, MII, MI.getDebugLoc(), TII->get(TargetOpcode::COPY), Dest)
                .addImm(0);
            MI.eraseFromParent();
            Changed = true;
            MII = MBB.begin();
            continue;
          }
        }

        if (isLogicOp(Opc)) {
          if (auto AVXOpc = AVXOpcodeMap.lookup(Opc)) {
            Register Dest = MI.getOperand(0).getReg();
            Register Src1 = MI.getOperand(1).getReg();
            Register Src2 = MI.getOperand(2).getReg();
            
            DebugLoc DL = MI.getDebugLoc();
            BuildMI(MBB, MII, DL, TII->get(AVXOpc), Dest)
                .addReg(Src1)
                .addReg(Src2);
            
            MI.eraseFromParent();
            Changed = true;
            MII = MBB.begin();
            continue;
          }
        }

        ++MII;
      }
    }

    return Changed;
  }
};

char X86LogicOptPass::ID = 0;
} // namespace

static llvm::RegisterPass<X86LogicOptPass>
    X("x86-logic-opt", "X86 Logical Operations Optimization Pass", false,
      false);

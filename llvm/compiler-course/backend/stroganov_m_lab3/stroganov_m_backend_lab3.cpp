#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

namespace {
class X86LogicOptPass : public MachineFunctionPass {
public:
  static char ID;
  X86LogicOptPass() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
    MachineRegisterInfo &MRI = MF.getRegInfo();
    bool Changed = false;

    const DenseMap<unsigned, unsigned> AVXOpcodeMap = {
        {X86::PANDrr,   X86::VPANDrr},  {X86::PORrr,    X86::VPORrr},
        {X86::PXORrr,   X86::VPXORrr},  {X86::ANDPSrr,  X86::VANDPSrr},
        {X86::ORPSrr,   X86::VORPSrr},  {X86::XORPSrr,  X86::VXORPSrr},
        {X86::PANDNrr,  X86::VPANDNrr}};

    auto convertToAVX = [&](MachineInstr &MI, MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator &MII) -> bool {
      auto NewOpc = AVXOpcodeMap.lookup(MI.getOpcode());
      if (!NewOpc || MI.getNumOperands() < 3)
        return false;

      Register Dest = MI.getOperand(0).getReg();
      Register Src1 = MI.getOperand(1).getReg();
      Register Src2 = MI.getOperand(2).getReg();

      BuildMI(MBB, MII, MI.getDebugLoc(), TII->get(NewOpc), Dest)
          .addReg(Src1)
          .addReg(Src2);
      return true;
    };

    for (MachineBasicBlock &MBB : MF) {
      for (auto MII = MBB.begin(), MIE = MBB.end(); MII != MIE;) {
        MachineInstr &MI = *MII;
        unsigned Opc = MI.getOpcode();

        if (!AVXOpcodeMap.count(Opc)) {
          ++MII;
          continue;
        }

        if (MI.getNumOperands() >= 2 && MI.getOperand(1).isReg()) {
          Register IntermediateReg = MI.getOperand(1).getReg();
          MachineInstr *DefMI = MRI.getUniqueVRegDef(IntermediateReg);

          if (DefMI && MRI.hasOneUse(IntermediateReg) &&
              AVXOpcodeMap.count(DefMI->getOpcode())) {
            auto NextII = std::next(MII);

            auto DefII = MachineBasicBlock::iterator(DefMI);
            if (convertToAVX(*DefMI, MBB, DefII)) {
              MBB.erase(DefMI);
              Changed = true;
            }

            if (convertToAVX(MI, MBB, MII)) {
              MII = MBB.erase(MII);
              Changed = true;
              continue;
            }

            MII = NextII;
          }
        }

        if (convertToAVX(MI, MBB, MII)) {
          MII = MBB.erase(MII);
          Changed = true;
        } else {
          ++MII;
        }
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


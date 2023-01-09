// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
#include "llvm/IR/InstIterator.h"
#include "llvm/Transforms/Obfuscation/CryptoUtils.h"
#include "llvm/Transforms/Obfuscation/Obfuscation.h"
#include "llvm/Transforms/Obfuscation/compat/LegacyLowerSwitch.h"
#include <fcntl.h>
using namespace llvm;

namespace {
struct Flattening : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  bool flag;
  Flattening() : FunctionPass(ID) { this->flag = true; }
  Flattening(bool flag) : FunctionPass(ID) { this->flag = flag; }
  bool runOnFunction(Function &F) override;
  bool flatten(Function *f);
};
} // namespace

char Flattening::ID = 0;
FunctionPass *llvm::createFlatteningPass(bool flag) {
  return new Flattening(flag);
}
INITIALIZE_PASS(Flattening, "cffobf", "Enable Control Flow Flattening.", true,
                true)
bool Flattening::runOnFunction(Function &F) {
  Function *tmp = &F;
  // Do we obfuscate
  if (toObfuscate(flag, tmp, "fla")) {
    if (F.isPresplitCoroutine())
      return false;
    errs() << "Running ControlFlowFlattening On " << F.getName() << "\n";
    flatten(tmp);
  }

  return true;
}

bool Flattening::flatten(Function *f) {
  vector<BasicBlock *> origBB;
  vector<BasicBlock *> exceptBB;
  BasicBlock *loopEntry;
  BasicBlock *loopEnd;
  LoadInst *load;
  SwitchInst *switchI;
  AllocaInst *switchVar;

  // SCRAMBLER
  map<uint32_t, uint32_t> scrambling_key;
  // END OF SCRAMBLER

  // Lower switch
  createLegacyLowerSwitchPass()->runOnFunction(*f);

  for (BasicBlock &BB : *f)
    if (BB.isLandingPad()) {
      exceptBB.emplace_back(&BB);
      for (unsigned int i = 0; i < BB.getTerminator()->getNumSuccessors(); i++)
        exceptBB.emplace_back(BB.getTerminator()->getSuccessor(i));
    }

  // Save all non exepcetion handle original BB
  for (BasicBlock &BB : *f) {
    if (std::find(exceptBB.begin(), exceptBB.end(), &BB) == exceptBB.end())
      origBB.emplace_back(&BB);

    if (!isa<BranchInst>(BB.getTerminator()) &&
        !isa<ReturnInst>(BB.getTerminator()) &&
        !isa<InvokeInst>(BB.getTerminator()) &&
        !isa<ResumeInst>(BB.getTerminator()) &&
        !isa<UnreachableInst>(BB.getTerminator()))
      return false;
  }

  // Nothing to flatten
  if (origBB.size() <= 1)
    return false;

  // Remove first BB
  origBB.erase(origBB.begin());

  // Get a pointer on the first BB
  Function::iterator tmp = f->begin(); //++tmp;
  BasicBlock *insert = &*tmp;

  // If main begin with an if or throw
  Instruction *br = nullptr;
  if (isa<BranchInst>(insert->getTerminator()) ||
      isa<InvokeInst>(insert->getTerminator()))
    br = insert->getTerminator();

  if (br) { // https://github.com/eshard/obfuscator-llvm/commit/af789724563ff3d300317fe4a9a9b0f3a88007eb
    BasicBlock::iterator i = insert->end();
    --i;

    if (insert->size() > 1)
      --i;

    BasicBlock *tmpBB = insert->splitBasicBlock(i, "first");
    origBB.insert(origBB.begin(), tmpBB);
  }

  // Remove jump
  Instruction *oldTerm = insert->getTerminator();

  // Create switch variable and set as it
  switchVar = new AllocaInst(Type::getInt32Ty(f->getContext()), 0, "switchVar",
                             oldTerm);
  oldTerm->eraseFromParent();
  new StoreInst(
      ConstantInt::get(Type::getInt32Ty(f->getContext()),
                       llvm::cryptoutils->scramble32(0, scrambling_key)),
      switchVar, insert);

  // Create main loop
  loopEntry = BasicBlock::Create(f->getContext(), "loopEntry", f, insert);
  loopEnd = BasicBlock::Create(f->getContext(), "loopEnd", f, insert);

  load = new LoadInst(switchVar->getAllocatedType(), switchVar, "switchVar",
                      loopEntry);

  // Move first BB on top
  insert->moveBefore(loopEntry);
  BranchInst::Create(loopEntry, insert);

  // loopEnd jump to loopEntry
  BranchInst::Create(loopEntry, loopEnd);

  BasicBlock *swDefault =
      BasicBlock::Create(f->getContext(), "switchDefault", f, loopEnd);
  BranchInst::Create(loopEnd, swDefault);

  // Create switch instruction itself and set condition
  switchI = SwitchInst::Create(&*f->begin(), swDefault, 0, loopEntry);
  switchI->setCondition(load);

  // Remove branch jump from 1st BB and make a jump to the while
  f->begin()->getTerminator()->eraseFromParent();

  BranchInst::Create(loopEntry, &*f->begin());

  // Put all BB in the switch
  for (BasicBlock *i : origBB) {
    ConstantInt *numCase = nullptr;

    // Move the BB inside the switch (only visual, no code logic)
    i->moveBefore(loopEnd);

    // Add case to switch
    numCase = cast<ConstantInt>(ConstantInt::get(
        switchI->getCondition()->getType(),
        llvm::cryptoutils->scramble32(switchI->getNumCases(), scrambling_key)));
    switchI->addCase(numCase, i);
  }

  // Recalculate switchVar
  for (BasicBlock *i : origBB) {
    ConstantInt *numCase = nullptr;

    if (i->getTerminator()->getOpcode() == Instruction::Invoke) {
      // Get next case
      numCase = switchI->findCaseDest(i->getTerminator()->getSuccessor(0));
      // If next case == default case (switchDefault)
      if (!numCase) {
        numCase = cast<ConstantInt>(
            ConstantInt::get(switchI->getCondition()->getType(),
                             llvm::cryptoutils->scramble32(
                                 switchI->getNumCases() - 1, scrambling_key)));
      }
      InvokeInst *II = dyn_cast<InvokeInst>(i->getTerminator());
      // Update switchVar and jump to the end of loop
      new StoreInst(numCase, load->getPointerOperand(), II);
      II->setNormalDest(loopEnd);
      continue;
    }

    // Ret BB
    if (i->getTerminator()->getNumSuccessors() == 0) {
      continue;
    }

    // If it's a non-conditional jump
    if (i->getTerminator()->getNumSuccessors() == 1) {
      // Get successor and delete terminator
      BasicBlock *succ = i->getTerminator()->getSuccessor(0);
      i->getTerminator()->eraseFromParent();

      // Get next case
      numCase = switchI->findCaseDest(succ);

      // If next case == default case (switchDefault)
      if (!numCase) {
        numCase = cast<ConstantInt>(
            ConstantInt::get(switchI->getCondition()->getType(),
                             llvm::cryptoutils->scramble32(
                                 switchI->getNumCases() - 1, scrambling_key)));
      }

      // Update switchVar and jump to the end of loop
      new StoreInst(numCase, load->getPointerOperand(), i);
      BranchInst::Create(loopEnd, i);
      continue;
    }

    // If it's a conditional jump
    if (i->getTerminator()->getNumSuccessors() == 2) {
      // Get next cases
      ConstantInt *numCaseTrue =
          switchI->findCaseDest(i->getTerminator()->getSuccessor(0));
      ConstantInt *numCaseFalse =
          switchI->findCaseDest(i->getTerminator()->getSuccessor(1));

      // Check if next case == default case (switchDefault)
      if (!numCaseTrue) {
        numCaseTrue = cast<ConstantInt>(
            ConstantInt::get(switchI->getCondition()->getType(),
                             llvm::cryptoutils->scramble32(
                                 switchI->getNumCases() - 1, scrambling_key)));
      }

      if (!numCaseFalse) {
        numCaseFalse = cast<ConstantInt>(
            ConstantInt::get(switchI->getCondition()->getType(),
                             llvm::cryptoutils->scramble32(
                                 switchI->getNumCases() - 1, scrambling_key)));
      }

      // Create a SelectInst
      BranchInst *br = cast<BranchInst>(i->getTerminator());
      SelectInst *sel =
          SelectInst::Create(br->getCondition(), numCaseTrue, numCaseFalse, "",
                             i->getTerminator());

      // Erase terminator
      i->getTerminator()->eraseFromParent();
      // Update switchVar and jump to the end of loop
      new StoreInst(sel, load->getPointerOperand(), i);
      BranchInst::Create(loopEnd, i);
      continue;
    }
  }
  errs() << "Fixing Stack\n";
  fixStack(f);
  errs() << "Fixed Stack\n";

  return true;
}

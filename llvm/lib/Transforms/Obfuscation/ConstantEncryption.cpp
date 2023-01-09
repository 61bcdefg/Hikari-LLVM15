/*
    LLVM ConstantEncryption Pass
    Copyright (C) 2017 Zhang(http://mayuyu.io)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#if LLVM_VERSION_MAJOR >= 15
#include "llvm/IR/IntrinsicInst.h"
#endif
#include "llvm/Pass.h"
#include "llvm/Transforms/Obfuscation/Obfuscation.h"
#include "llvm/Transforms/Obfuscation/SubstituteImpl.h"
#include "llvm/Transforms/Obfuscation/compat/CallSite.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
using namespace llvm;
using namespace std;
static cl::opt<bool>
    SubstituteXor("constenc_subxor",
                  cl::desc("Substitute xor operator of ConstantEncryption"),
                  cl::value_desc("Substitute xor operator"), cl::init(false),
                  cl::Optional);
static cl::opt<bool>
    ConstToGV("constenc_togv",
              cl::desc("Replace ConstantInt with GlobalVariable"),
              cl::value_desc("ConstantInt to GlobalVariable"), cl::init(false),
              cl::Optional);
static cl::opt<unsigned int>
    ObfProbRate("constenc_prob",
                cl::desc("Choose the probability [%] each instructions will be "
                         "obfuscated by the ConstantEncryption pass"),
                cl::value_desc("probability rate"), cl::init(50), cl::Optional);
static cl::opt<int> ObfTimes(
    "constenc_times",
    cl::desc(
        "Choose how many time the ConstantEncryption pass loop on a function"),
    cl::value_desc("Number of Times"), cl::init(1), cl::Optional);
namespace llvm {
struct ConstantEncryption : public ModulePass {
  static char ID;
  bool flag;
  ConstantEncryption(bool flag) : ModulePass(ID) { this->flag = flag; }
  ConstantEncryption() : ModulePass(ID) { this->flag = true; }
  bool canEncryptConstant(Instruction *I) {
    if (isa<IntrinsicInst>(I) || isa<GetElementPtrInst>(I) || isa<PHINode>(I) ||
        I->isAtomic())
      return false;
    if (!(cryptoutils->get_range(100) <= ObfProbRate))
      return false;
    return true;
  }
  bool runOnModule(Module &M) override {
    if (ObfProbRate > 100) {
      errs() << "ConstantEncryption application instruction percentage "
                "-constenc_prob=x must be 0 < x <= 100";
      return false;
    }
    for (Function &F : M)
      if (toObfuscate(flag, &F, "constenc") && !F.isPresplitCoroutine()) {
        errs() << "Running ConstantEncryption On " << F.getName() << "\n";
        int times = ObfTimes;
        while (times) {
          for (Instruction &I : instructions(F)) {
            if (!canEncryptConstant(&I))
              continue;
            for (unsigned i = 0; i < I.getNumOperands(); i++) {
              if (isa<SwitchInst>(&I) && i != 0)
                break;
              Value *Op = I.getOperand(i);
              if (isa<ConstantInt>(Op))
                HandleConstantIntOperand(&I, i);
              if (GlobalVariable *G =
                      dyn_cast<GlobalVariable>(Op->stripPointerCasts()))
                if (G->hasInitializer() &&
                    (G->hasPrivateLinkage() || G->hasInternalLinkage()) &&
                    isa<ConstantInt>(G->getInitializer()))
                  HandleConstantIntInitializerGV(G);
            }
          }
          if (ConstToGV)
            for (Instruction &I : instructions(F)) {
              if (!canEncryptConstant(&I))
                continue;
              for (unsigned int i = 0; i < I.getNumOperands(); i++)
                if (ConstantInt *CI = dyn_cast<ConstantInt>(I.getOperand(i))) {
                  if (isa<SwitchInst>(&I) && i != 0)
                    break;
                  GlobalVariable *GV = new GlobalVariable(
                      M, CI->getType(), false,
                      GlobalValue::LinkageTypes::PrivateLinkage,
                      ConstantInt::get(CI->getType(), CI->getValue()),
                      "ConstantEncryptionConstToGlobal");
                  appendToCompilerUsed(M, GV);
                  I.setOperand(i, new LoadInst(GV->getValueType(), GV, "", &I));
                }
            }
          times--;
        }
      }
    return true;
  }

  void HandleConstantIntInitializerGV(GlobalVariable *GVPtr) {
    // Prepare Types and Keys
    ConstantInt *CI = dyn_cast<ConstantInt>(GVPtr->getInitializer());
    pair<ConstantInt * /*key*/, ConstantInt * /*new*/> keyandnew =
        PairConstantInt(CI);
    ConstantInt *XORKey = keyandnew.first;
    ConstantInt *newGVInit = keyandnew.second;
    if (!XORKey || !newGVInit)
      return;
    GVPtr->setInitializer(newGVInit);
    for (User *U : GVPtr->users()) {
      BinaryOperator *XORInst = nullptr;
      if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
        XORInst = BinaryOperator::Create(Instruction::Xor, LI, XORKey);
        XORInst->insertAfter(LI);
        LI->replaceAllUsesWith(XORInst);
        XORInst->setOperand(0, LI);
      } else if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
        XORInst =
            BinaryOperator::Create(Instruction::Xor, SI->getOperand(0), XORKey);
        XORInst->insertAfter(SI);
        SI->replaceUsesOfWith(SI->getOperand(0), XORInst);
      }
      if (SubstituteXor)
        SubstituteImpl::substituteXor(XORInst);
    }
  }

  void HandleConstantIntOperand(Instruction *I, unsigned opindex) {
    pair<ConstantInt * /*key*/, ConstantInt * /*new*/> keyandnew =
        PairConstantInt(cast<ConstantInt>(I->getOperand(opindex)));
    ConstantInt *Key = keyandnew.first;
    ConstantInt *New = keyandnew.second;
    if (!Key || !New)
      return;
    BinaryOperator *NewOperand =
        BinaryOperator::Create(Instruction::Xor, New, Key, "", I);
    I->setOperand(opindex, NewOperand);
    if (SubstituteXor)
      SubstituteImpl::substituteXor(NewOperand);
  }

  pair<ConstantInt * /*key*/, ConstantInt * /*new*/>
  PairConstantInt(ConstantInt *C) {
    IntegerType *IT = cast<IntegerType>(C->getType());
    uint64_t K;
    if (IT->getBitWidth() == 1 || IT->getBitWidth() == 8)
      K = cryptoutils->get_uint8_t();
    else if (IT->getBitWidth() == 16)
      K = cryptoutils->get_uint16_t();
    else if (IT->getBitWidth() == 32)
      K = cryptoutils->get_uint32_t();
    else if (IT->getBitWidth() == 64)
      K = cryptoutils->get_uint64_t();
    else
      return make_pair(nullptr, nullptr);
    ConstantInt *CI =
        cast<ConstantInt>(ConstantInt::get(IT, K ^ C->getValue()));
    return make_pair(ConstantInt::get(IT, K), CI);
  }
};

ModulePass *createConstantEncryptionPass(bool flag) {
  return new ConstantEncryption(flag);
}
} // namespace llvm
char ConstantEncryption::ID = 0;
INITIALIZE_PASS(ConstantEncryption, "constenc",
                "Enable ConstantInt GV Encryption.", true, true)
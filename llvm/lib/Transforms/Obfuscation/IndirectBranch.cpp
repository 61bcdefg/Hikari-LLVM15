// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Obfuscation/Obfuscation.h"
#include "llvm/Transforms/Obfuscation/SubstituteImpl.h"
#include "llvm/Transforms/Obfuscation/compat/LegacyLowerSwitch.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;
using namespace std;
static cl::opt<bool>
    UseStack("indibran-use-stack", cl::init(false), cl::NotHidden,
             cl::desc("[IndirectBranch]Stack-based indirect jumps"));
static cl::opt<bool>
    EncryptJumpTarget("indibran-enc-jump-target", cl::init(false),
                      cl::NotHidden,
                      cl::desc("[IndirectBranch]Encrypt jump target"));
namespace llvm {
struct IndirectBranch : public FunctionPass {
  static char ID;
  bool flag;
  bool initialized;
  map<BasicBlock *, unsigned long long> indexmap;
  map<Function *, ConstantInt *> encmap;
  IndirectBranch() : FunctionPass(ID) {
    this->flag = true;
    this->initialized = false;
  }
  IndirectBranch(bool flag) : FunctionPass(ID) {
    this->flag = flag;
    this->initialized = false;
  }
  StringRef getPassName() const override { return "IndirectBranch"; }
  bool initialize(Module &M) {
    vector<Constant *> BBs;
    unsigned long long i = 0;
    for (Function &F : M) {
      // See https://github.com/NeHyci/Hikari-LLVM15/issues/32
      createLegacyLowerSwitchPass()->runOnFunction(F);
      if (EncryptJumpTarget)
        encmap[&F] = ConstantInt::get(
            Type::getInt32Ty(M.getContext()),
            cryptoutils->get_range(UINT8_MAX, UINT16_MAX * 2) * 4);
      for (BasicBlock &BB : F)
        if (!BB.isEntryBlock()) {
          indexmap[&BB] = i++;
          BBs.emplace_back(EncryptJumpTarget
                               ? ConstantExpr::getGetElementPtr(
                                     Type::getInt8Ty(M.getContext()),
                                     ConstantExpr::getBitCast(
                                         BlockAddress::get(&BB),
                                         Type::getInt8PtrTy(M.getContext())),
                                     encmap[&F])
                               : BlockAddress::get(&BB));
        }
    }
    ArrayType *AT =
        ArrayType::get(Type::getInt8PtrTy(M.getContext()), BBs.size());
    Constant *BlockAddressArray =
        ConstantArray::get(AT, ArrayRef<Constant *>(BBs));
    GlobalVariable *Table = new GlobalVariable(
        M, AT, false, GlobalValue::LinkageTypes::PrivateLinkage,
        BlockAddressArray, "IndirectBranchingGlobalTable");
    appendToCompilerUsed(M, {Table});
    this->initialized = true;
    return true;
  }
  bool runOnFunction(Function &Func) override {
    if (!toObfuscate(flag, &Func, "indibr"))
      return false;
    if (!this->initialized)
      initialize(*Func.getParent());
    errs() << "Running IndirectBranch On " << Func.getName() << "\n";
    vector<BranchInst *> BIs;
    for (Instruction &Inst : instructions(Func))
      if (BranchInst *BI = dyn_cast<BranchInst>(&Inst))
        BIs.emplace_back(BI);

    Value *zero =
        ConstantInt::get(Type::getInt32Ty(Func.getParent()->getContext()), 0);
    for (BranchInst *BI : BIs) {
      IRBuilder<NoFolder> IRB(BI);
      vector<BasicBlock *> BBs;
      // We use the condition's evaluation result to generate the GEP
      // instruction  False evaluates to 0 while true evaluates to 1.  So here
      // we insert the false block first
      if (BI->isConditional() && !BI->getSuccessor(1)->isEntryBlock())
        BBs.emplace_back(BI->getSuccessor(1));
      if (!BI->getSuccessor(0)->isEntryBlock())
        BBs.emplace_back(BI->getSuccessor(0));

      GlobalVariable *LoadFrom = nullptr;
      if (BI->isConditional() ||
          indexmap.find(BI->getSuccessor(0)) == indexmap.end()) {
        ArrayType *AT = ArrayType::get(
            Type::getInt8PtrTy(Func.getParent()->getContext()), BBs.size());
        vector<Constant *> BlockAddresses;
        for (BasicBlock *BB : BBs)
          BlockAddresses.emplace_back(
              EncryptJumpTarget
                  ? ConstantExpr::getGetElementPtr(
                        Type::getInt8Ty(Func.getParent()->getContext()),
                        ConstantExpr::getBitCast(
                            BlockAddress::get(BB),
                            Type::getInt8PtrTy(Func.getParent()->getContext())),
                        encmap[&Func])
                  : BlockAddress::get(BB));
        // Create a new GV
        Constant *BlockAddressArray =
            ConstantArray::get(AT, ArrayRef<Constant *>(BlockAddresses));
        LoadFrom = new GlobalVariable(
            *Func.getParent(), AT, false,
            GlobalValue::LinkageTypes::PrivateLinkage, BlockAddressArray,
            "HikariConditionalLocalIndirectBranchingTable");
        appendToCompilerUsed(*Func.getParent(), {LoadFrom});
      } else {
        LoadFrom = Func.getParent()->getGlobalVariable(
            "IndirectBranchingGlobalTable", true);
      }
      Value *index, *RealIndex = nullptr;
      if (BI->isConditional()) {
        Value *condition = BI->getCondition();
        index = IRB.CreateZExt(
            condition, Type::getInt32Ty(Func.getParent()->getContext()));
        RealIndex = index;
      } else {
        Value *indexval = nullptr;
        ConstantInt *IndexEncKey =
            EncryptJumpTarget
                ? ConstantInt::get(
                      Type::getInt32Ty(Func.getParent()->getContext()),
                      cryptoutils->get_uint32_t())
                : nullptr;
        if (EncryptJumpTarget) {
          GlobalVariable *indexgv = new GlobalVariable(
              *Func.getParent(),
              Type::getInt32Ty(Func.getParent()->getContext()), false,
              GlobalValue::LinkageTypes::PrivateLinkage,
              ConstantInt::get(Type::getInt32Ty(Func.getParent()->getContext()),
                               IndexEncKey->getValue() ^
                                   indexmap[BI->getSuccessor(0)]),
              "IndirectBranchingIndex");
          appendToCompilerUsed(*Func.getParent(), {indexgv});
          indexval = IRB.CreateLoad(indexgv->getValueType(), indexgv);
        } else {
          indexval =
              ConstantInt::get(Type::getInt32Ty(Func.getParent()->getContext()),
                               indexmap[BI->getSuccessor(0)]);
        }
        if (UseStack) {
          AllocaInst *AI = IRB.CreateAlloca(indexval->getType());
          IRB.CreateStore(indexval, AI);
          index = IRB.CreateLoad(AI->getAllocatedType(), AI);
        } else {
          index = indexval;
        }
        RealIndex =
            EncryptJumpTarget ? IRB.CreateXor(index, IndexEncKey) : index;
      }
      Value *LI, *enckeyLoad, *gepptr = nullptr;
      if (UseStack) {
        AllocaInst *AI = IRB.CreateAlloca(LoadFrom->getType());
        AllocaInst *AI2 = IRB.CreateAlloca(index->getType());
        IRB.CreateStore(LoadFrom, AI);
        IRB.CreateStore(RealIndex, AI2);
        Value *GEP = IRB.CreateGEP(
            LoadFrom->getValueType(), IRB.CreateLoad(LoadFrom->getType(), AI),
            {zero, IRB.CreateLoad(index->getType(), AI2)});
        AllocaInst *AI3 = IRB.CreateAlloca(GEP->getType());
        IRB.CreateStore(GEP, AI3);
        gepptr = IRB.CreateLoad(Type::getInt8PtrTy(Func.getContext()),
                                IRB.CreateLoad(AI3->getAllocatedType(), AI3));
        if (!EncryptJumpTarget)
          LI = IRB.CreateLoad(AI3->getAllocatedType(),
                              IRB.CreateLoad(AI3->getAllocatedType(), AI3),
                              "IndirectBranchingTargetAddress");
      } else {
        Value *GEP = IRB.CreateGEP(LoadFrom->getValueType(), LoadFrom,
                                   {zero, RealIndex});
        gepptr = IRB.CreateLoad(Type::getInt8PtrTy(Func.getContext()), GEP);
        if (!EncryptJumpTarget)
          LI = IRB.CreateLoad(GEP->getType(), GEP,
                              "IndirectBranchingTargetAddress");
      }
      if (EncryptJumpTarget) {
        ConstantInt *encenckey = ConstantInt::get(
            Type::getInt32Ty(Func.getContext()), cryptoutils->get_uint32_t());
        GlobalVariable *enckeyGV = new GlobalVariable(
            *Func.getParent(), Type::getInt32Ty(Func.getParent()->getContext()),
            false, GlobalValue::LinkageTypes::PrivateLinkage,
            ConstantInt::get(Type::getInt32Ty(Func.getContext()),
                             encenckey->getValue() ^ encmap[&Func]->getValue()),
            "IndirectBranchingAddressEncryptKey");
        appendToCompilerUsed(*Func.getParent(), enckeyGV);
        enckeyLoad = IRB.CreateXor(
            IRB.CreateLoad(enckeyGV->getValueType(), enckeyGV), encenckey);
        LI = IRB.CreateGEP(Type::getInt8Ty(Func.getContext()), gepptr,
                           IRB.CreateSub(zero, enckeyLoad),
                           "IndirectBranchingTargetAddress");
        SubstituteImpl::substituteXor(dyn_cast<BinaryOperator>(enckeyLoad));
        if (!BI->isConditional())
          SubstituteImpl::substituteXor(dyn_cast<BinaryOperator>(RealIndex));
      }
      IndirectBrInst *indirBr = IndirectBrInst::Create(LI, BBs.size());
      for (BasicBlock *BB : BBs)
        indirBr->addDestination(BB);
      ReplaceInstWithInst(BI, indirBr);
    }
    return true;
  }
};
} // namespace llvm

FunctionPass *llvm::createIndirectBranchPass(bool flag) {
  return new IndirectBranch(flag);
}
char IndirectBranch::ID = 0;
INITIALIZE_PASS(IndirectBranch, "indibran", "IndirectBranching", true, true)

// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
#include "llvm/Transforms/Obfuscation/StringEncryption.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Obfuscation/CryptoUtils.h"
#include "llvm/Transforms/Obfuscation/Obfuscation.h"
#include "llvm/Transforms/Obfuscation/Utils.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>

using namespace llvm;
using namespace std;

namespace llvm {
struct StringEncryption : public ModulePass {
  static char ID;
  map<Function * /*Function*/, GlobalVariable * /*Decryption Status*/>
      encstatus;
  map<GlobalVariable *, pair<Constant *, GlobalVariable *>> mgv2keys;
  vector<GlobalVariable *> genedgv;
  bool flag;
  StringEncryption() : ModulePass(ID) { this->flag = true; }

  StringEncryption(bool flag) : ModulePass(ID) { this->flag = flag; }

  StringRef getPassName() const override { return "StringEncryption"; }

  bool usersAllInOneFunction(GlobalVariable *GV) {
    vector<Instruction *> instusers;
    for (User *user : GV->users()) {
      if (Instruction *Inst = dyn_cast<Instruction>(user))
        instusers.emplace_back(Inst);
      else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(user)) {
        for (User *user2 : CE->users()) {
          if (Instruction *Inst = dyn_cast<Instruction>(user2))
            instusers.emplace_back(Inst);
          else
            return GV->getNumUses() == 1;
        }
      } else
        return GV->getNumUses() == 1;
    }
    Function *LastFuncOfInst = nullptr;
    for (Instruction *I : instusers) {
      if (LastFuncOfInst != nullptr && I->getFunction() != LastFuncOfInst)
        return false;
      LastFuncOfInst = I->getFunction();
    }
    return true;
  }

  bool handleableGV(GlobalVariable *GV) {
    if (GV->hasInitializer() && GV->getSection() != "llvm.metadata" &&
        GV->getSection() != "llvm.ptrauth" &&
        !(GV->getSection().contains("__objc") &&
          !GV->getSection().contains("array")) &&
        !GV->getName().contains("OBJC") &&
        ((GV->getLinkage() == GlobalValue::LinkageTypes::PrivateLinkage ||
          GV->getLinkage() == GlobalValue::LinkageTypes::InternalLinkage) &&
         (flag || usersAllInOneFunction(GV))) &&
        std::find(genedgv.begin(), genedgv.end(), GV) == genedgv.end())
      return true;
    return false;
  }

  bool runOnModule(Module &M) override {
    // in runOnModule. We simple iterate function list and dispatch functions
    // to handlers
    for (Function &F : M) {
      if (toObfuscate(flag, &F, "strenc")) {
        errs() << "Running StringEncryption On " << F.getName() << "\n";
        Constant *S =
            ConstantInt::getNullValue(Type::getInt32Ty(M.getContext()));
        GlobalVariable *GV = new GlobalVariable(
            M, S->getType(), false, GlobalValue::LinkageTypes::PrivateLinkage,
            S, "StringEncryptionEncStatus");
        encstatus[&F] = GV;
        HandleFunction(&F);
      }
    }
    return true;
  }

  void HandleFunction(Function *Func) {
    FixFunctionConstantExpr(Func);
    vector<GlobalVariable *> Globals;
    set<User *> Users;
    for (Instruction &I : instructions(Func))
      for (Value *Op : I.operands())
        if (GlobalVariable *G =
                dyn_cast<GlobalVariable>(Op->stripPointerCasts())) {
          if (User *U = dyn_cast<User>(Op))
            Users.insert(U);
          Users.insert(&I);
          // errs() << ">>>>> emplace_back(), global var name: " << G->getName()
          // << "\n";
          Globals.emplace_back(G);
        }
    set<GlobalVariable *> rawStrings;
    set<GlobalVariable *> objCStrings;
    map<GlobalVariable *, pair<Constant *, GlobalVariable *>> GV2Keys;
    map<GlobalVariable * /*old*/, pair<GlobalVariable * /*encrypted*/,
                                       GlobalVariable * /*decrypt space*/>>
        old2new;

    auto end = Globals.end();
    for (auto it = Globals.begin(); it != end; ++it) {
      end = std::remove(it + 1, end, *it);
    }
    Globals.erase(end, Globals.end());

    vector<GlobalVariable *> transedGlobals = {};

    do {
      for (GlobalVariable *GV : Globals) {
        if (std::find(transedGlobals.begin(), transedGlobals.end(), GV) ==
            transedGlobals.end()) {
          bool breakThisFor = false;
          if (handleableGV(GV)) {
            if (GlobalVariable *CastedGV = dyn_cast<GlobalVariable>(
                    GV->getInitializer()->stripPointerCasts())) {
              if (std::find(Globals.begin(), Globals.end(), CastedGV) ==
                  Globals.end()) {
                Globals.emplace_back(CastedGV);
                ConstantExpr *CE = dyn_cast<ConstantExpr>(GV->getInitializer());
                Users.insert(CE ? CE : GV->getInitializer());
                breakThisFor = true;
              }
            }
            if (GV->getInitializer()->getType() ==
                StructType::getTypeByName(Func->getParent()->getContext(),
                                          "struct.__NSConstantString_tag")) {
              objCStrings.insert(GV);
              rawStrings.insert(cast<GlobalVariable>(
                  cast<ConstantStruct>(GV->getInitializer())
                      ->getOperand(2)
                      ->stripPointerCasts()));
            } else if (isa<ConstantDataSequential>(GV->getInitializer())) {
              rawStrings.insert(GV);
            } else if (isa<ConstantStruct>(GV->getInitializer())) {
              ConstantStruct *CS = cast<ConstantStruct>(GV->getInitializer());
              for (unsigned i = 0; i < CS->getNumOperands(); i++) {
                Constant *Op = CS->getOperand(i);
                if (GlobalVariable *OpGV =
                        dyn_cast<GlobalVariable>(Op->stripPointerCasts())) {
                  if (!handleableGV(OpGV)) {
                    continue;
                  }
                  Users.insert(Op);
                  if (std::find(Globals.begin(), Globals.end(), OpGV) ==
                      Globals.end()) {
                    Globals.emplace_back(OpGV);
                    breakThisFor = true;
                  }
                }
              }
            } else if (isa<ConstantArray>(GV->getInitializer())) {
              ConstantArray *CA = dyn_cast<ConstantArray>(GV->getInitializer());
              for (unsigned j = 0; j < CA->getNumOperands(); j++) {
                Constant *Opp = CA->getOperand(j);
                if (GlobalVariable *OppGV =
                        dyn_cast<GlobalVariable>(Opp->stripPointerCasts())) {
                  if (!handleableGV(OppGV)) {
                    continue;
                  }
                  Users.insert(Opp);
                  if (std::find(Globals.begin(), Globals.end(), OppGV) ==
                      Globals.end()) {
                    Globals.emplace_back(OppGV);
                    breakThisFor = true;
                  }
                }
              }
            }
          } else if (std::find(genedgv.begin(), genedgv.end(), GV) !=
                     genedgv.end()) {
            pair<Constant *, GlobalVariable *> mgv2keysval = mgv2keys[GV];
            if (GV->getInitializer()->getType() ==
                StructType::getTypeByName(Func->getParent()->getContext(),
                                          "struct.__NSConstantString_tag")) {
              GlobalVariable *rawgv = cast<GlobalVariable>(
                  cast<ConstantStruct>(GV->getInitializer())
                      ->getOperand(2)
                      ->stripPointerCasts());
              mgv2keysval = mgv2keys[rawgv];
              if (mgv2keysval.first && mgv2keysval.second) {
                GV2Keys[rawgv] = mgv2keysval;
              }
            } else if (mgv2keysval.first && mgv2keysval.second) {
              GV2Keys[GV] = mgv2keysval;
            }
          }
          transedGlobals.emplace_back(GV);
          if (breakThisFor)
            break;
        }
      } // foreach loop
    } while (transedGlobals.size() != Globals.size());
    for (GlobalVariable *GV : rawStrings) {
      if (GV->getInitializer()->isZeroValue() ||
          GV->getInitializer()->isNullValue())
        continue;
      ConstantDataSequential *CDS =
          cast<ConstantDataSequential>(GV->getInitializer());
      Type *memberType = CDS->getElementType();
      // Ignore non-IntegerType
      if (!isa<IntegerType>(memberType))
        continue;
      IntegerType *intType = cast<IntegerType>(memberType);
      Constant *KeyConst = nullptr;
      Constant *EncryptedConst = nullptr;
      Constant *DummyConst = nullptr;
      if (intType == Type::getInt8Ty(GV->getParent()->getContext())) {
        vector<uint8_t> keys;
        vector<uint8_t> encry;
        vector<uint8_t> dummy;
        for (unsigned i = 0; i < CDS->getNumElements(); i++) {
          uint8_t K = cryptoutils->get_uint8_t();
          uint64_t V = CDS->getElementAsInteger(i);
          keys.emplace_back(K);
          encry.emplace_back(K ^ V);
          dummy.emplace_back(cryptoutils->get_uint8_t());
        }
        KeyConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                          ArrayRef<uint8_t>(keys));
        EncryptedConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                                ArrayRef<uint8_t>(encry));
        DummyConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                            ArrayRef<uint8_t>(dummy));

      } else if (intType == Type::getInt16Ty(GV->getParent()->getContext())) {
        vector<uint16_t> keys;
        vector<uint16_t> encry;
        vector<uint16_t> dummy;
        for (unsigned i = 0; i < CDS->getNumElements(); i++) {
          uint16_t K = cryptoutils->get_uint16_t();
          uint64_t V = CDS->getElementAsInteger(i);
          keys.emplace_back(K);
          encry.emplace_back(K ^ V);
          dummy.emplace_back(cryptoutils->get_uint16_t());
        }
        KeyConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                          ArrayRef<uint16_t>(keys));
        EncryptedConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                                ArrayRef<uint16_t>(encry));
        DummyConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                            ArrayRef<uint16_t>(dummy));
      } else if (intType == Type::getInt32Ty(GV->getParent()->getContext())) {
        vector<uint32_t> keys;
        vector<uint32_t> encry;
        vector<uint32_t> dummy;
        for (unsigned i = 0; i < CDS->getNumElements(); i++) {
          uint32_t K = cryptoutils->get_uint32_t();
          uint64_t V = CDS->getElementAsInteger(i);
          keys.emplace_back(K);
          encry.emplace_back(K ^ V);
          dummy.emplace_back(cryptoutils->get_uint32_t());
        }
        KeyConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                          ArrayRef<uint32_t>(keys));
        EncryptedConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                                ArrayRef<uint32_t>(encry));
        DummyConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                            ArrayRef<uint32_t>(dummy));
      } else if (intType == Type::getInt64Ty(GV->getParent()->getContext())) {
        vector<uint64_t> keys;
        vector<uint64_t> encry;
        vector<uint64_t> dummy;
        for (unsigned i = 0; i < CDS->getNumElements(); i++) {
          uint64_t K = cryptoutils->get_uint64_t();
          uint64_t V = CDS->getElementAsInteger(i);
          keys.emplace_back(K);
          encry.emplace_back(K ^ V);
          dummy.emplace_back(cryptoutils->get_uint32_t());
        }
        KeyConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                          ArrayRef<uint64_t>(keys));
        EncryptedConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                                ArrayRef<uint64_t>(encry));
        DummyConst = ConstantDataArray::get(GV->getParent()->getContext(),
                                            ArrayRef<uint64_t>(dummy));
      } else {
        errs() << "Unsupported CDS Type\n";
        abort();
      }
      // Prepare new rawGV
      GlobalVariable *EncryptedRawGV = new GlobalVariable(
          *(GV->getParent()), EncryptedConst->getType(), false,
          GV->getLinkage(), EncryptedConst, "EncryptedString", nullptr,
          GV->getThreadLocalMode(), GV->getType()->getAddressSpace());
      genedgv.emplace_back(EncryptedRawGV);
      GlobalVariable *DecryptSpaceGV = new GlobalVariable(
          *(GV->getParent()), DummyConst->getType(), false, GV->getLinkage(),
          DummyConst, "DecryptSpace", nullptr, GV->getThreadLocalMode(),
          GV->getType()->getAddressSpace());
      genedgv.emplace_back(DecryptSpaceGV);
      old2new[GV] = make_pair(EncryptedRawGV, DecryptSpaceGV);
      GV2Keys[DecryptSpaceGV] = make_pair(KeyConst, EncryptedRawGV);
      mgv2keys[DecryptSpaceGV] = GV2Keys[DecryptSpaceGV];
    }
    // Now prepare ObjC new GV
    for (GlobalVariable *GV : objCStrings) {
      ConstantStruct *CS = cast<ConstantStruct>(GV->getInitializer());
      GlobalVariable *oldrawString =
          cast<GlobalVariable>(CS->getOperand(2)->stripPointerCasts());
      if (old2new.find(oldrawString) ==
          old2new.end()) // Filter out zero initializers
        continue;
      GlobalVariable *EncryptedOCGV =
          ObjectiveCString(GV, "EncryptedStringObjC", oldrawString,
                           old2new[oldrawString].first, CS);
      genedgv.emplace_back(EncryptedOCGV);
      GlobalVariable *DecryptSpaceOCGV =
          ObjectiveCString(GV, "DecryptSpaceObjC", oldrawString,
                           old2new[oldrawString].second, CS);
      genedgv.emplace_back(DecryptSpaceOCGV);
      old2new[GV] = make_pair(EncryptedOCGV, DecryptSpaceOCGV);
    } // End prepare ObjC new GV
    if (GV2Keys.empty())
      return;
    // Replace Uses
    for (User *U : Users) {
      for (map<GlobalVariable *,
               pair<GlobalVariable *, GlobalVariable *>>::iterator iter =
               old2new.begin();
           iter != old2new.end(); ++iter) {
        U->replaceUsesOfWith(iter->first, iter->second.second);
        iter->first->removeDeadConstantUsers();
      }
    } // End Replace Uses
      // CleanUp Old ObjC GVs
    for (GlobalVariable *GV : objCStrings)
      if (GV->getNumUses() == 0) {
        GV->dropAllReferences();
        old2new.erase(GV);
        GV->eraseFromParent();
      }
    // CleanUp Old Raw GVs
    for (map<GlobalVariable *,
             pair<GlobalVariable *, GlobalVariable *>>::iterator iter =
             old2new.begin();
         iter != old2new.end(); ++iter) {
      GlobalVariable *toDelete = iter->first;
      toDelete->removeDeadConstantUsers();
      if (toDelete->getNumUses() == 0) {
        toDelete->dropAllReferences();
        toDelete->eraseFromParent();
      }
    }
    GlobalVariable *StatusGV = encstatus[Func];
    /*
       - Split Original EntryPoint BB into A and C.
       - Create new BB as Decryption BB between A and C. Adjust the terminators
         into: A (Alloca a new array containing all)
               |
               B(If not decrypted)
               |
               C
     */
    BasicBlock *A = &(Func->getEntryBlock());
    BasicBlock *C = A->splitBasicBlock(A->getFirstNonPHIOrDbgOrLifetime());
    C->setName("PrecedingBlock");
    BasicBlock *B =
        BasicBlock::Create(Func->getContext(), "StringDecryptionBB", Func, C);
    // Change A's terminator to jump to B
    // We'll add new terminator to jump C later
    BranchInst *newBr = BranchInst::Create(B);
    ReplaceInstWithInst(A->getTerminator(), newBr);
    // Insert DecryptionCode
    HandleDecryptionBlock(B, C, GV2Keys);
    IRBuilder<> IRB(A->getFirstNonPHIOrDbgOrLifetime());
    // Add atomic load checking status in A
    LoadInst *LI = IRB.CreateLoad(StatusGV->getValueType(), StatusGV,
                                  "LoadEncryptionStatus");
    LI->setAtomic(
        AtomicOrdering::Acquire); // Will be released at the start of C
    LI->setAlignment(Align(4));
    Value *condition = IRB.CreateICmpEQ(
        LI, ConstantInt::get(Type::getInt32Ty(Func->getContext()), 0));
    A->getTerminator()->eraseFromParent();
    BranchInst::Create(B, C, condition, A);
    // Add StoreInst atomically in C start
    // No matter control flow is coming from A or B, the GVs must be decrypted
    StoreInst *SI =
        new StoreInst(ConstantInt::get(Type::getInt32Ty(Func->getContext()), 1),
                      StatusGV, C->getFirstNonPHIOrDbgOrLifetime());
    SI->setAlignment(Align(4));
    SI->setAtomic(AtomicOrdering::Release); // Release the lock acquired in LI
  }                                         // End of HandleFunction

  GlobalVariable *ObjectiveCString(GlobalVariable *GV, string name,
                                   GlobalVariable *oldrawString,
                                   GlobalVariable *newString,
                                   ConstantStruct *CS) {
    Value *zero = ConstantInt::get(Type::getInt32Ty(GV->getContext()), 0);
    vector<Constant *> vals;
    vals.emplace_back(CS->getOperand(0));
    vals.emplace_back(CS->getOperand(1));
    Constant *GEPed = ConstantExpr::getInBoundsGetElementPtr(
        newString->getValueType(), newString, {zero, zero});
    if (GEPed->getType() == CS->getOperand(2)->getType()) {
      vals.emplace_back(GEPed);
    } else {
      Constant *BitCasted =
          ConstantExpr::getBitCast(newString, CS->getOperand(2)->getType());
      vals.emplace_back(BitCasted);
    }
    vals.emplace_back(CS->getOperand(3));
    Constant *newCS =
        ConstantStruct::get(CS->getType(), ArrayRef<Constant *>(vals));
    GlobalVariable *ObjcGV = new GlobalVariable(
        *(GV->getParent()), newCS->getType(), false, GV->getLinkage(), newCS,
        name, nullptr, GV->getThreadLocalMode(),
        GV->getType()->getAddressSpace());
    // for arm64e target on Apple LLVM
    if (hasApplePtrauth(GV->getParent())) {
      GlobalVariable *PtrauthGV = cast<GlobalVariable>(
          cast<ConstantExpr>(newCS->getOperand(0))->getOperand(0));
      if (PtrauthGV->getSection() == "llvm.ptrauth" &&
          cast<GlobalVariable>(
              GV->getParent()->getContext().supportsTypedPointers()
                  ? cast<ConstantExpr>(
                        PtrauthGV->getInitializer()->getOperand(2))
                        ->getOperand(0)
                  : PtrauthGV->getInitializer()->getOperand(2))
                  ->getGlobalIdentifier() != ObjcGV->getGlobalIdentifier()) {
        GlobalVariable *NewPtrauthGV = new GlobalVariable(
            *PtrauthGV->getParent(), PtrauthGV->getValueType(), true,
            PtrauthGV->getLinkage(),
            ConstantStruct::getAnon(
                {(Constant *)PtrauthGV->getInitializer()->getOperand(0),
                 (ConstantInt *)PtrauthGV->getInitializer()->getOperand(1),
                 ConstantExpr::getPtrToInt(
                     ObjcGV, Type::getInt64Ty(ObjcGV->getContext())),
                 (ConstantInt *)PtrauthGV->getInitializer()->getOperand(3)},
                false),
            PtrauthGV->getName(), nullptr, PtrauthGV->getThreadLocalMode());
        NewPtrauthGV->setSection("llvm.ptrauth");
        NewPtrauthGV->setAlignment(Align(8));
        if (PtrauthGV->getNumUses() == 0) {
          PtrauthGV->dropAllReferences();
          PtrauthGV->eraseFromParent();
        }
        ObjcGV->getInitializer()->setOperand(
            0,
            ConstantExpr::getBitCast(
                NewPtrauthGV, Type::getInt32PtrTy(NewPtrauthGV->getContext())));
      }
    }
    return ObjcGV;
  }

  void HandleDecryptionBlock(
      BasicBlock *B, BasicBlock *C,
      map<GlobalVariable *, pair<Constant *, GlobalVariable *>> &GV2Keys) {
    IRBuilder<> IRB(B);
    Value *zero = ConstantInt::get(Type::getInt32Ty(B->getContext()), 0);
    for (map<GlobalVariable *, pair<Constant *, GlobalVariable *>>::iterator
             iter = GV2Keys.begin();
         iter != GV2Keys.end(); ++iter) {
      ConstantDataArray *CastedCDA =
          cast<ConstantDataArray>(iter->second.first);
      // Prevent optimization of encrypted data
      appendToCompilerUsed(*iter->second.second->getParent(),
                           {iter->second.second});
      // Element-By-Element XOR so the fucking verifier won't complain
      // Also, this hides keys
      for (uint64_t i = 0; i < CastedCDA->getType()->getNumElements(); i++) {
        Value *offset = ConstantInt::get(Type::getInt64Ty(B->getContext()), i);
        Value *EncryptedGEP =
            IRB.CreateGEP(iter->second.second->getValueType(),
                          iter->second.second, {zero, offset});
        Value *DecryptedGEP = IRB.CreateGEP(iter->first->getValueType(),
                                            iter->first, {zero, offset});
        LoadInst *LI = IRB.CreateLoad(CastedCDA->getElementType(), EncryptedGEP,
                                      "EncryptedChar");
        Value *XORed = IRB.CreateXor(LI, CastedCDA->getElementAsConstant(i));
        IRB.CreateStore(XORed, DecryptedGEP);
      }
    }
    IRB.CreateBr(C);
  } // End of HandleDecryptionBlock
};

ModulePass *createStringEncryptionPass(bool flag) {
  return new StringEncryption(flag);
}
} // namespace llvm

char StringEncryption::ID = 0;
INITIALIZE_PASS(StringEncryption, "strcry", "Enable String Encryption", true,
                true)

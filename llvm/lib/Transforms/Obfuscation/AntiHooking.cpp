/*
    LLVM Anti Hooking Pass
    Copyright (C) 2017 Zhang(https://github.com/Naville/)

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
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Obfuscation/Obfuscation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/ADT/Triple.h"
#if LLVM_VERSION_MAJOR >= 11
#include "llvm/Transforms/Obfuscation/compat/CallSite.h"
#else
#include "llvm/IR/CallSite.h"
#endif
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#define AARCH64_SIGNATURE_BR_X16 0xd61f0200
#define AARCH64_SIGNATURE_BR_X17 0xd61f0220

static cl::opt<string>
    PreCompiledIRPath("adhexrirpath",
                      cl::desc("External Path Pointing To Pre-compiled Anti "
                               "Hooking Handler IR.See Wiki"),
                      cl::value_desc("filename"), cl::init(""));

using namespace llvm;
using namespace std;
namespace llvm {
struct AntiHook : public ModulePass {
  static char ID;
  bool flag;
  bool initialized;
  bool opaquepointers;
  bool appleptrauth;
  AntiHook() : ModulePass(ID) { this->flag = true;this->initialized=false;}
  AntiHook(bool flag) : ModulePass(ID) { this->flag = flag;this->initialized=false;}
  StringRef getPassName() const override { return "AntiHook"; }
  bool Initialize(Module &M){
    if (PreCompiledIRPath == "") {
      SmallString<32> Path;
      if (sys::path::home_directory(Path)) { // Stolen from LineEditor.cpp
        sys::path::append(Path, "Hikari");
        Triple tri(M.getTargetTriple());
        sys::path::append(Path, "PrecompiledAntiHooking-" +
                                    Triple::getArchTypeName(tri.getArch()) +
                                    "-" + Triple::getOSTypeName(tri.getOS()) +
                                    ".bc");
        PreCompiledIRPath = Path.c_str();
      }
    }
    ifstream f(PreCompiledIRPath);
    if (f.good()) {
      errs() << "Linking PreCompiled AntiHooking IR From:" << PreCompiledIRPath
             << "\n";
      SMDiagnostic SMD;
      unique_ptr<Module> ADBM(
          parseIRFile(StringRef(PreCompiledIRPath), SMD, M.getContext()));
      Linker::linkModules(M, std::move(ADBM), Linker::Flags::OverrideFromSrc);
    } else {
      errs() << "Failed To Link PreCompiled AntiHooking IR From:"
             << PreCompiledIRPath << "\n";
    }
    opaquepointers = !M.getContext().supportsTypedPointers();
    appleptrauth = M.getModuleFlag("ptrauth.abi-version");

    if (Triple(M.getTargetTriple()).getVendor() == Triple::VendorType::Apple) {
      bool hasobjcmethod = false;
      for (GlobalVariable &GV : M.globals()) {
        if (GV.hasName() && GV.hasInitializer() &&
            (GV.getName().startswith("_OBJC_$_INSTANCE_METHODS") ||
             GV.getName().startswith("_OBJC_$_CLASS_METHODS"))) {
          hasobjcmethod = true;
          break;
        }
      }
      if (!hasobjcmethod)
        return true;
      Type *Int8PtrTy = Type::getInt8PtrTy(M.getContext());
      M.getOrInsertFunction("objc_getClass",
                            FunctionType::get(Int8PtrTy, {Int8PtrTy}, false));
      M.getOrInsertFunction("sel_registerName",
                            FunctionType::get(Int8PtrTy, {Int8PtrTy}, false));
      FunctionType *IMPType =
          FunctionType::get(Int8PtrTy, {Int8PtrTy, Int8PtrTy}, true);
      PointerType *IMPPointerType = PointerType::getUnqual(IMPType);
      M.getOrInsertFunction("method_getImplementation", FunctionType::get(IMPPointerType,
              {PointerType::getUnqual(StructType::getTypeByName(
                                                                              M.getContext(), "struct._objc_method"))},
              false));
      M.getOrInsertFunction("class_getInstanceMethod",
          FunctionType::get(
              PointerType::getUnqual(StructType::getTypeByName(
                                    M.getContext(), "struct._objc_method")),
              {Int8PtrTy, Int8PtrTy}, false));
      M.getOrInsertFunction("class_getClassMethod",
          FunctionType::get(
              PointerType::getUnqual(StructType::getTypeByName(
                                    M.getContext(), "struct._objc_method")),
              {Int8PtrTy, Int8PtrTy}, false));
    }
    return true;
  }

  bool runOnModule(Module &M) override {
    Triple Tri(M.getTargetTriple());
    for (Function &F : M) {
      if (toObfuscate(flag, &F, "antihook")) {
        errs() << "Running AntiHooking On " << F.getName() << "\n";
        if (!this->initialized) {
          Initialize(M);
          this->initialized = true;
        }
        if (Tri.isAArch64()) {
          vector<Function *> calledFunctions;
          calledFunctions.emplace_back(&F);
          HandleInlineHookAArch64(&F, calledFunctions);
        }
      }
    }
    if (Tri.getVendor() == Triple::VendorType::Apple) {
      for (GlobalVariable &GV : M.globals()) {
        if (GV.hasName() && GV.hasInitializer() &&
            (GV.getName().startswith("_OBJC_$_INSTANCE_METHODS") ||
             GV.getName().startswith("_OBJC_$_CLASS_METHODS"))) {
          ConstantExpr *methodListCE = opaquepointers ? nullptr : cast<ConstantExpr>(&GV);
          GlobalVariable *methodListGV = opaquepointers ? &GV : cast<GlobalVariable>(methodListCE->getOperand(0));
          ConstantStruct *methodListStruct = cast<ConstantStruct>(methodListGV);
          ConstantArray *method_list =
              cast<ConstantArray>(methodListStruct->getOperand(2));
          for (unsigned i = 0; i < method_list->getNumOperands(); i++) {
            ConstantStruct *methodStruct =
                cast<ConstantStruct>(method_list->getOperand(i));
            GlobalVariable *SELNameGV = cast<GlobalVariable>(opaquepointers ? methodStruct->getOperand(0)
                               : methodStruct->getOperand(0)->getOperand(0));
            ConstantDataSequential *SELNameCDS =
                cast<ConstantDataSequential>(SELNameGV->getInitializer());
            bool classmethod = GV.getName().startswith("_OBJC_$_CLASS_METHODS");
            string classname = GV.getName().substr(strlen(classmethod ? "_OBJC_$_CLASS_METHODS_"
                                               : "_OBJC_$_INSTANCE_METHODS_")).str();
            string selname = SELNameCDS->getAsCString().str();
            Function *IMPFunc = cast<Function>(appleptrauth ? opaquepointers ? cast<GlobalVariable>(methodStruct->getOperand(2))
                                ->getInitializer()->getOperand(0)
                          : cast<ConstantExpr>(cast<GlobalVariable>(
                                    cast<ConstantExpr>(methodStruct->getOperand(2))
                                        ->getOperand(0))->getInitializer()->getOperand(0))
                                ->getOperand(0)->getOperand(0)
                : opaquepointers ? methodStruct->getOperand(2)
                                 : methodStruct->getOperand(2)->getOperand(0));
            if (!toObfuscate(flag, IMPFunc, "antihook"))
              continue;
            HandleObjcRuntimeHook(IMPFunc, classname, selname, classmethod);
          }
        }
      }
    }
    // TODO: Make fishhook unavailable
    return true;
  } // End runOnFunction
  void HandleInlineHookAArch64(Function *F, vector<Function *> &FunctionsToDetect) {
    // First we locate an insert point to check ourself
    // The following is equivalent to
    //     if (*(uint32_t *)(Function + 4) == SIGNATURE || *(uint32_t *)(Function + 8) == SIGNATURE)
    //       Handler();
    //

    /*
   We split the originalBB A into:
      A < - InlineHook Detection
      | \
      |  B for handler()
      | /
      C < - Original Following BB
   */

    uint32_t signatures[] = {AARCH64_SIGNATURE_BR_X16, AARCH64_SIGNATURE_BR_X17};

    for (Function *called : FunctionsToDetect) {
      BasicBlock *A = &(F->getEntryBlock());
      BasicBlock *C = A->splitBasicBlock(A->getFirstNonPHIOrDbgOrLifetime());
      BasicBlock *A2 = BasicBlock::Create(A->getContext(), "", F, C);
      BasicBlock *B = BasicBlock::Create(A->getContext(), "HookDetectedHandler", F, C);
      // Change A's terminator to jump to B
      // We'll add new terminator in B to jump C later
      A->getTerminator()->eraseFromParent();

      IRBuilder<> IRBA(A);
      IRBuilder<> IRBA2(A2);
      IRBuilder<> IRBB(B);

      Type *Int64Ty = Type::getInt64Ty(F->getContext());
      Type *Int32Ty = Type::getInt32Ty(F->getContext());
      Type *Int32PtrTy = Type::getInt32PtrTy(F->getContext());

      BasicBlock *SwDefault = BasicBlock::Create(F->getContext(), "SwitchDefault", F);
      BasicBlock *SwDefault2 = BasicBlock::Create(F->getContext(), "SwitchDefault2", F);
      BranchInst::Create(A2, SwDefault);
      BranchInst::Create(C, SwDefault2);
      Value *toDetect = IRBA.CreateLoad(Int32Ty, IRBA.CreateIntToPtr(
                       IRBA.CreateAdd(IRBA.CreatePtrToInt(called, Int64Ty), ConstantInt::get(Int64Ty, 4)), Int32PtrTy));
      Value *toDetect2 = IRBA2.CreateLoad(Int32Ty, IRBA2.CreateIntToPtr(
                       IRBA2.CreateAdd(IRBA2.CreatePtrToInt(called, Int64Ty), ConstantInt::get(Int64Ty, 8)), Int32PtrTy));
      SwitchInst *SI = IRBA.CreateSwitch(A, SwDefault, 0);
      SwitchInst *SI2 = IRBA2.CreateSwitch(A2, SwDefault2, 0);
      SI->setCondition(toDetect);
      SI2->setCondition(toDetect2);
      for (uint32_t sig : signatures) {
        SI->addCase(ConstantInt::get(IntegerType::get(F->getContext(), 32), sig), B);
        SI2->addCase(ConstantInt::get(IntegerType::get(F->getContext(), 32), sig), B);
      }

      Function *AHCallBack = F->getParent()->getFunction("AHCallBack");
      if (AHCallBack) {
        IRBB.CreateCall(AHCallBack);
      } else {
        Triple Tri(F->getParent()->getTargetTriple());
        if (Tri.isOSDarwin() && Tri.getArch() == Triple::ArchType::aarch64) {
          string exitsvcasm = "mov w16, #1\n";
          exitsvcasm += "svc #" + to_string(cryptoutils->get_range(65536)) + "\n";
          InlineAsm *IA = InlineAsm::get(FunctionType::get(IRBB.getVoidTy(), false),
                             exitsvcasm, "", true, false);
          IRBB.CreateCall(IA);
        }
        else {
          // First. Build up declaration for abort();
          FunctionType *ABFT =
              FunctionType::get(Type::getVoidTy(A->getContext()), false);
          Function *abort_declare = cast<Function>(
              F->getParent()->getOrInsertFunction("abort", ABFT).getCallee());
          abort_declare->addFnAttr(Attribute::AttrKind::NoReturn);
          IRBB.CreateCall(abort_declare);
        }
      }
      IRBB.CreateBr(C); // Insert a new br into the end of B to jump back to C
    }
  }

  void HandleObjcRuntimeHook(Function *ObjcMethodImp, string classname, string selname, bool classmethod) {
    /*
    We split the originalBB A into:
       A < - RuntimeHook Detection
       | \
       |  B for handler()
       | /
       C < - Original Following BB
    */
    Module *M = ObjcMethodImp->getParent();

    BasicBlock *A = &(ObjcMethodImp->getEntryBlock());
    BasicBlock *C = A->splitBasicBlock(A->getFirstNonPHIOrDbgOrLifetime());
    BasicBlock *B = BasicBlock::Create(A->getContext(), "HookDetectedHandler",
                                       ObjcMethodImp, C);
    // Delete A's terminator
    A->getTerminator()->eraseFromParent();

    IRBuilder<> IRBA(A);
    IRBuilder<> IRBB(B);

    Type *Int8PtrTy = Type::getInt8PtrTy(M->getContext());

    Value *GetClass = IRBA.CreateCall(M->getFunction("objc_getClass"), {IRBA.CreateGlobalStringPtr(classname)});
    Value *GetSelector = IRBA.CreateCall(M->getFunction("sel_registerName"),
                        {IRBA.CreateGlobalStringPtr(selname)});
    Value *GetMethod =
        IRBA.CreateCall(M->getFunction(classmethod ? "class_getClassMethod" : "class_getInstanceMethod"), {GetClass, GetSelector});
    Value *GetMethodImp = IRBA.CreateCall(M->getFunction("method_getImplementation"), {GetMethod});
    Value *IcmpEq =
        IRBA.CreateICmpEQ(IRBA.CreateBitCast(GetMethodImp, Int8PtrTy),
                          ConstantExpr::getBitCast(ObjcMethodImp, Int8PtrTy));
    IRBA.CreateCondBr(IcmpEq, C, B);

    Function *AHCallBack = M->getFunction("AHCallBack");
    if (AHCallBack) {
      IRBB.CreateCall(AHCallBack);
    } else {
      Triple Tri(M->getTargetTriple());
      if (Tri.isOSDarwin() && Tri.isAArch64()) {
        string exitsvcasm = "mov w16, #1\n";
        exitsvcasm += "svc #" + to_string(cryptoutils->get_range(65536)) + "\n";
        InlineAsm *IA =
            InlineAsm::get(FunctionType::get(IRBB.getVoidTy(), false),
                           exitsvcasm, "", true, false);
        IRBB.CreateCall(IA);
      } else {
        // First. Build up declaration for abort();
        FunctionType *ABFT =
            FunctionType::get(Type::getVoidTy(A->getContext()), false);
        Function *abort_declare = cast<Function>(M->getOrInsertFunction("abort", ABFT).getCallee());
        abort_declare->addFnAttr(Attribute::AttrKind::NoReturn);
        IRBB.CreateCall(abort_declare);
      }
    }
    IRBB.CreateBr(C); // Insert a new br into the end of B to jump back to C
  }
};
} // namespace llvm

ModulePass *llvm::createAntiHookPass(bool flag) { return new AntiHook(flag); }
char AntiHook::ID = 0;
INITIALIZE_PASS(AntiHook, "antihook", "AntiHook", true, true)

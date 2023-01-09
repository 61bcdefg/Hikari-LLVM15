// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
#include "json.hpp"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Obfuscation/Obfuscation.h"
#include "llvm/Transforms/Obfuscation/compat/CallSite.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
using namespace llvm;
using namespace std;
#if LLVM_VERSION_MAJOR <= 14
using json = nlohmann::json;
#endif
static const int DARWIN_FLAG = 0x2 | 0x8;
static const int ANDROID64_FLAG = 0x00002 | 0x100;
static const int ANDROID32_FLAG = 0x0000 | 0x2;
static cl::opt<uint64_t>
    dlopen_flag("fco_flag",
                cl::desc("The value of RTLD_DEFAULT on your platform"),
                cl::value_desc("value"), cl::init(-1), cl::Optional);
static cl::opt<string>
    SymbolConfigPath("fcoconfig",
                     cl::desc("FunctionCallObfuscate Configuration Path"),
                     cl::value_desc("filename"), cl::init("+-x/"));
namespace llvm {
struct FunctionCallObfuscate : public FunctionPass {
  static char ID;
#if LLVM_VERSION_MAJOR >= 15
  nlohmann::json Configuration;
#else
  json Configuration;
#endif
  bool flag;
  bool initialized;
  bool objchandled;
  FunctionCallObfuscate() : FunctionPass(ID) {
    this->flag = true;
    this->initialized = false;
    this->objchandled = false;
  }
  FunctionCallObfuscate(bool flag) : FunctionPass(ID) {
    this->flag = flag;
    this->initialized = false;
    this->objchandled = false;
  }
  StringRef getPassName() const override { return "FunctionCallObfuscate"; }
  bool initialize(Module &M) {
    // Basic Defs
    if (SymbolConfigPath == "+-x/") {
      SmallString<32> Path;
      if (sys::path::home_directory(Path)) { // Stolen from LineEditor.cpp
        sys::path::append(Path, "Hikari", "SymbolConfig.json");
        SymbolConfigPath = Path.c_str();
      }
    }
    ifstream infile(SymbolConfigPath);
    if (infile.good()) {
      errs() << "Loading Symbol Configuration From:" << SymbolConfigPath
             << "\n";
      infile >> this->Configuration;
    } else {
      errs() << "Failed To Load Symbol Configuration From:" << SymbolConfigPath
             << "\n";
    }
    Triple tri(M.getTargetTriple());
    if (tri.getVendor() == Triple::VendorType::Apple) {
      Type *Int8PtrTy = Type::getInt8PtrTy(M.getContext());
      // Generic ObjC Runtime Declarations
      FunctionType *IMPType =
          FunctionType::get(Int8PtrTy, {Int8PtrTy, Int8PtrTy}, true);
      PointerType *IMPPointerType = PointerType::get(IMPType, 0);
      FunctionType *class_replaceMethod_type = FunctionType::get(
          IMPPointerType, {Int8PtrTy, Int8PtrTy, IMPPointerType, Int8PtrTy},
          false);
      M.getOrInsertFunction("class_replaceMethod", class_replaceMethod_type);
      FunctionType *sel_registerName_type =
          FunctionType::get(Int8PtrTy, {Int8PtrTy}, false);
      M.getOrInsertFunction("sel_registerName", sel_registerName_type);
      FunctionType *objc_getClass_type =
          FunctionType::get(Int8PtrTy, {Int8PtrTy}, false);
      M.getOrInsertFunction("objc_getClass", objc_getClass_type);
      M.getOrInsertFunction("objc_getMetaClass", objc_getClass_type);
      FunctionType *class_getName_Type =
          FunctionType::get(Int8PtrTy, {Int8PtrTy}, false);
      M.getOrInsertFunction("class_getName", class_getName_Type);
      FunctionType *objc_getMetaClass_Type =
          FunctionType::get(Int8PtrTy, {Int8PtrTy}, false);
      M.getOrInsertFunction("objc_getMetaClass", objc_getMetaClass_Type);
    }
    this->initialized = true;
    return true;
  }
  void HandleObjC(Module &M) {
    vector<Instruction *> toErase;
    // Iterate all CLASSREF uses and replace with objc_getClass() call
    // Strings are encrypted in other passes
    for (GlobalVariable &GV : M.globals()) {
      if (!GV.hasName() || !GV.hasInitializer() || GV.user_empty())
        continue;
      if (GV.getName().contains("OBJC_CLASSLIST_REFERENCES")) {
        string className = GV.getInitializer()->getName().str();
        className.replace(className.find("OBJC_CLASS_$_"),
                          strlen("OBJC_CLASS_$_"), "");
        for (User *U : GV.users()) {
          if (Instruction *I = dyn_cast<Instruction>(U)) {
            IRBuilder<> builder(I);
            Function *objc_getClass_Func =
                cast<Function>(M.getFunction("objc_getClass"));
            Value *newClassName =
                builder.CreateGlobalStringPtr(StringRef(className));
            CallInst *CI =
                builder.CreateCall(objc_getClass_Func, {newClassName});
            // We need to bitcast it back to avoid IRVerifier
            Value *BCI = builder.CreateBitCast(CI, I->getType());
            I->replaceAllUsesWith(BCI);
            toErase.emplace_back(I); // We cannot erase it directly or we will
                                     // have problems releasing the IRBuilder.
          }
        }
        GV.removeDeadConstantUsers();
        if (GV.getNumUses() == 0) {
          GV.dropAllReferences();
          GV.eraseFromParent();
        }
      }
      // Selector Convert
      else if (GV.getName().contains("OBJC_SELECTOR_REFERENCES")) {
        ConstantExpr *CE = M.getContext().supportsTypedPointers()
                               ? dyn_cast<ConstantExpr>(GV.getInitializer())
                               : nullptr;
        Constant *C = M.getContext().supportsTypedPointers()
                          ? CE->getOperand(0)
                          : GV.getInitializer();
        GlobalVariable *SELNameGV = dyn_cast<GlobalVariable>(C);
        ConstantDataArray *CDA =
            dyn_cast<ConstantDataArray>(SELNameGV->getInitializer());
        StringRef SELName = CDA->getAsString(); // This is REAL Selector Name
        for (User *U : GV.users()) {
          if (Instruction *I = dyn_cast<Instruction>(U)) {
            IRBuilder<> builder(I);
            Function *sel_registerName_Func =
                cast<Function>(M.getFunction("sel_registerName"));
            Value *newGlobalSELName = builder.CreateGlobalStringPtr(SELName);
            CallInst *CI =
                builder.CreateCall(sel_registerName_Func, {newGlobalSELName});
            // We need to bitcast it back to avoid IRVerifier
            Value *BCI = builder.CreateBitCast(CI, I->getType());
            I->replaceAllUsesWith(BCI);
            toErase.emplace_back(I); // We cannot erase it directly or we will
                                     // have problems releasing the IRBuilder.
          }
        }
        GV.removeDeadConstantUsers();
        if (GV.getNumUses() == 0) {
          GV.dropAllReferences();
          GV.eraseFromParent();
        }
      }
    }
    for (Instruction *I : toErase)
      I->eraseFromParent();
    objchandled = true;
  }
  bool runOnFunction(Function &F) override {
    // Construct Function Prototypes
    if (!toObfuscate(flag, &F, "fco"))
      return false;
    Triple Tri(F.getParent()->getTargetTriple());
    if (!Tri.isAndroid() && !Tri.isOSDarwin()) {
      errs() << "Unsupported Target Triple: "
             << F.getParent()->getTargetTriple() << "\n";
      return false;
    }
    errs() << "Running FunctionCallObfuscate On " << F.getName() << "\n";
    Module *M = F.getParent();
    if (!this->initialized)
      initialize(*M);
    FixFunctionConstantExpr(&F);
    if (!this->objchandled)
      HandleObjC(*M);
    Type *Int32Ty = Type::getInt32Ty(M->getContext());
    Type *Int8PtrTy = Type::getInt8PtrTy(M->getContext());
    // ObjC Runtime Declarations
    FunctionType *dlopen_type = FunctionType::get(
        Int8PtrTy, {Int8PtrTy, Int32Ty},
        false); // int has a length of 32 on both 32/64bit platform
    FunctionType *dlsym_type =
        FunctionType::get(Int8PtrTy, {Int8PtrTy, Int8PtrTy}, false);
    Function *dlopen_decl = cast<Function>(
        M->getOrInsertFunction("dlopen", dlopen_type).getCallee());
    Function *dlsym_decl =
        cast<Function>(M->getOrInsertFunction("dlsym", dlsym_type).getCallee());
    // Begin Iteration
    for (BasicBlock &BB : F) {
      for (Instruction &Inst : BB) {
        if (isa<CallInst>(&Inst) || isa<InvokeInst>(&Inst)) {
          CallSite CS(&Inst);
          Function *calledFunction = CS.getCalledFunction();
          if (!calledFunction) {
            /*
              Note:
              For Indirect Calls:
                CalledFunction is NULL and calledValue is usually a bitcasted
              function pointer. We'll need to strip out the hiccups and obtain
              the called Function* from there
            */
            calledFunction =
                dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());
          }
          // Simple Extracting Failed
          // Use our own implementation
          if (!calledFunction)
            continue;
          // It's only safe to restrict our modification to external symbols
          // Otherwise stripped binary will crash
          if (!calledFunction->empty() ||
              calledFunction->getName().equals("dlsym") ||
              calledFunction->getName().equals("dlopen") ||
              calledFunction->isIntrinsic())
            continue;

          if (this->Configuration.find(calledFunction->getName().str()) !=
              this->Configuration.end()) {
            string sname = this->Configuration[calledFunction->getName().str()]
                               .get<string>();
            StringRef calledFunctionName = StringRef(sname);
            BasicBlock *EntryBlock = CS->getParent();
            if (Tri.isOSDarwin()) {
              dlopen_flag = DARWIN_FLAG;
            } else if (Tri.isAndroid()) {
              if (Tri.isArch64Bit())
                dlopen_flag = ANDROID64_FLAG;
              else
                dlopen_flag = ANDROID32_FLAG;
            } else {
              errs() << "[FunctionCallObfuscate] Unsupported Target Triple:"
                     << F.getParent()->getTargetTriple() << "\n";
              errs() << "[FunctionCallObfuscate] Applying Default Signature:"
                     << dlopen_flag << "\n";
            }
            IRBuilder<> IRB(EntryBlock, EntryBlock->getFirstInsertionPt());
            Value *Handle = IRB.CreateCall(
                dlopen_decl, {Constant::getNullValue(Int8PtrTy),
                              ConstantInt::get(Int32Ty, dlopen_flag)});
            // Create dlsym call
            Value *fp = IRB.CreateCall(
                dlsym_decl,
                {Handle, IRB.CreateGlobalStringPtr(calledFunctionName)});
            Value *bitCastedFunction =
                IRB.CreateBitCast(fp, CS.getCalledValue()->getType());
            CS.setCalledFunction(bitCastedFunction);
          }
        }
      }
    }
    return true;
  }
};
FunctionPass *createFunctionCallObfuscatePass(bool flag) {
  return new FunctionCallObfuscate(flag);
}
} // namespace llvm
char FunctionCallObfuscate::ID = 0;
INITIALIZE_PASS(FunctionCallObfuscate, "fcoobf",
                "Enable Function CallSite Obfuscation.", true, true)

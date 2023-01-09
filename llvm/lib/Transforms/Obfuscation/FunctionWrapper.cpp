// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Obfuscation/FunctionWrapper.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Obfuscation/CryptoUtils.h"
#include "llvm/Transforms/Obfuscation/Utils.h"
#include "llvm/Transforms/Obfuscation/compat/CallSite.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
using namespace llvm;
using namespace std;
static cl::opt<int>
    ProbRate("fw_prob",
             cl::desc("Choose the probability [%] For Each CallSite To Be "
                      "Obfuscated By FunctionWrapper"),
             cl::value_desc("Probability Rate"), cl::init(30), cl::Optional);
static cl::opt<int> ObfTimes(
    "fw_times",
    cl::desc(
        "Choose how many time the FunctionWrapper pass loop on a CallSite"),
    cl::value_desc("Number of Times"), cl::init(2), cl::Optional);
namespace llvm {
struct FunctionWrapper : public ModulePass {
  static char ID;
  bool flag;
  FunctionWrapper() : ModulePass(ID) { this->flag = true; }
  FunctionWrapper(bool flag) : ModulePass(ID) { this->flag = flag; }
  StringRef getPassName() const override { return "FunctionWrapper"; }
  bool runOnModule(Module &M) override {
    if (ProbRate > 100) {
      errs() << "FunctionWrapper application CallSite percentage "
                "-fw_prob=x must be 0 < x <= 100";
      return false;
    }
    vector<CallSite *> callsites;
    for (Function &F : M) {
      if (toObfuscate(flag, &F, "fw")) {
        errs() << "Running FunctionWrapper On " << F.getName() << "\n";
        for (Instruction &Inst : instructions(F))
          if ((isa<CallInst>(&Inst) || isa<InvokeInst>(&Inst)))
            if ((int)llvm::cryptoutils->get_range(100) <= ProbRate)
              callsites.emplace_back(new CallSite(&Inst));
      }
    }
    for (CallSite *CS : callsites)
      for (int i = 0; i < ObfTimes && CS != nullptr; i++)
        CS = HandleCallSite(CS);
    return true;
  } // End of runOnModule
  CallSite *HandleCallSite(CallSite *CS) {
    Value *calledFunction = CS->getCalledFunction();
    if (calledFunction == nullptr)
      calledFunction = CS->getCalledValue()->stripPointerCasts();
    // Filter out IndirectCalls that depends on the context
    // Otherwise It'll be blantantly troublesome since you can't reference an
    // Instruction outside its BB  Too much trouble for a hobby project
    // To be precise, we only keep CS that refers to a non-intrinsic function
    // either directly or through casting
    if (calledFunction == nullptr ||
        (!isa<ConstantExpr>(calledFunction) &&
         !isa<Function>(calledFunction)) ||
        CS->getIntrinsicID() != Intrinsic::not_intrinsic)
      return nullptr;
    vector<unsigned int> byvalArgNums = {};
    if (Function *tmp = dyn_cast<Function>(calledFunction)) {
      if (tmp->getName().startswith("clang.")) {
        // Clang Intrinsic
        return nullptr;
      }
      for (Argument &arg : tmp->args()) {
        if (arg.hasStructRetAttr() || arg.hasSwiftSelfAttr()) {
          return nullptr;
        }
        if (arg.hasByValAttr())
          byvalArgNums.emplace_back(arg.getArgNo());
      }
    }
    // Create a new function which in turn calls the actual function
    vector<Type *> types;
    for (unsigned int i = 0; i < CS->getNumArgOperands(); i++)
      types.emplace_back(CS->getArgOperand(i)->getType());
    FunctionType *ft =
        FunctionType::get(CS->getType(), ArrayRef<Type *>(types), false);
    Function *func =
        Function::Create(ft, GlobalValue::LinkageTypes::InternalLinkage,
                         "HikariFunctionWrapper", CS->getParent()->getModule());
    func->setCallingConv(CS->getCallingConv());
    // Trolling was all fun and shit so old implementation forced this symbol to
    // exist in all objects
    appendToCompilerUsed(*func->getParent(), {func});
    BasicBlock *BB = BasicBlock::Create(func->getContext(), "", func);
    vector<Value *> params;
    if (byvalArgNums.empty())
      for (Argument &arg : func->args())
        params.emplace_back(&arg);
    else
      for (Argument &arg : func->args()) {
        if (std::find(byvalArgNums.begin(), byvalArgNums.end(),
                      arg.getArgNo()) != byvalArgNums.end()) {
          params.emplace_back(&arg);
        } else {
          AllocaInst *AI = nullptr;
          if (!BB->empty()) {
            BasicBlock::iterator InsertPoint = BB->begin();
            while (isa<AllocaInst>(InsertPoint))
              ++InsertPoint;
            AI = new AllocaInst(arg.getType(), 0, "", &*InsertPoint);
          } else
            AI = new AllocaInst(arg.getType(), 0, "", BB);
          new StoreInst(&arg, AI, BB);
          LoadInst *LI = new LoadInst(AI->getAllocatedType(), AI, "", BB);
          params.emplace_back(LI);
        }
      }
    Value *retval = CallInst::Create(
        CS->getFunctionType(),
        ConstantExpr::getBitCast(cast<Function>(calledFunction),
                                 CS->getCalledValue()->getType()),
        ArrayRef<Value *>(params), None, "", BB);
    if (ft->getReturnType()->isVoidTy()) {
      ReturnInst::Create(BB->getContext(), BB);
    } else {
      ReturnInst::Create(BB->getContext(), retval, BB);
    }
    CS->setCalledFunction(func);
    CS->mutateFunctionType(ft);
    Instruction *Inst = CS->getInstruction();
    delete CS;
    return new CallSite(Inst);
  }
};

ModulePass *createFunctionWrapperPass(bool flag) {
  return new FunctionWrapper(flag);
}
} // namespace llvm

char FunctionWrapper::ID = 0;
INITIALIZE_PASS(FunctionWrapper, "funcwra", "Enable FunctionWrapper.", true,
                true)

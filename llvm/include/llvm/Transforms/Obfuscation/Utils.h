#ifndef __UTILS_OBF__
#define __UTILS_OBF__

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/Local.h" // For DemoteRegToStack and DemotePHIToStack
#include <stdio.h>
#include <sstream>
#include <map>
#include <set>
#if LLVM_VERSION_MAJOR >= 15
#include "llvm/IR/Constants.h"
#endif
void fixStack(llvm::Function *f);
void LowerInvokeInst(llvm::Function &F);
std::string readAnnotate(llvm::Function *f);
std::map<llvm::GlobalValue*,llvm::StringRef> BuildAnnotateMap(llvm::Module& M);
bool toObfuscate(bool flag, llvm::Function *f, std::string attribute);
void FixBasicBlockConstantExpr(llvm::BasicBlock *BB);
void FixFunctionConstantExpr(llvm::Function *Func);
void appendToAnnotations(llvm::Module &M,llvm::ConstantStruct *Data);
#endif

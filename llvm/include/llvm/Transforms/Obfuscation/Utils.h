#ifndef __UTILS_OBF__
#define __UTILS_OBF__

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/Local.h" // For DemoteRegToStack and DemotePHIToStack
#include <map>
#include <set>
#include <sstream>
#include <stdio.h>
#if LLVM_VERSION_MAJOR >= 15
#include "llvm/IR/Constants.h"
#endif
void fixStack(llvm::Function *f);
std::string readAnnotate(llvm::Function *f);
bool toObfuscate(bool flag, llvm::Function *f, std::string attribute);
bool hasApplePtrauth(llvm::Module *M);
void FixBasicBlockConstantExpr(llvm::BasicBlock *BB);
void FixFunctionConstantExpr(llvm::Function *Func);
#if 0
void appendToAnnotations(llvm::Module &M,llvm::ConstantStruct *Data);
std::map<llvm::GlobalValue*,llvm::StringRef> BuildAnnotateMap(llvm::Module& M);
#endif
#endif

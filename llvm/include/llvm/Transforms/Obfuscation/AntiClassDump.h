#ifndef _ANTI_CLASSDUMP_H_
#define _ANTI_CLASSDUMP_H_
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
using namespace std;
using namespace llvm;

// Namespace
namespace llvm {
ModulePass *createAntiClassDumpPass();
void initializeAntiClassDumpPass(PassRegistry &Registry);
} // namespace llvm
#endif

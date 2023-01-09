#ifndef _INDIRECT_BRANCH_H_
#define _INDIRECT_BRANCH_H_
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
using namespace std;
using namespace llvm;

// Namespace
namespace llvm {
FunctionPass *createIndirectBranchPass(bool flag);
void initializeIndirectBranchPass(PassRegistry &Registry);
} // namespace llvm
#endif

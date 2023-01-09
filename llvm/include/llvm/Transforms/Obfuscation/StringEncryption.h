#ifndef _STRING_ENCRYPTION_H_
#define _STRING_ENCRYPTION_H_
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
using namespace std;
using namespace llvm;

// Namespace
namespace llvm {
ModulePass *createStringEncryptionPass(bool flag);
void initializeStringEncryptionPass(PassRegistry &Registry);
} // namespace llvm
#endif

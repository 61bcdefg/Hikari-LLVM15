#ifndef _CONSTANT_ENCRYPTION_H_
#define _CONSTANT_ENCRYPTION_H_
#include "llvm/Pass.h"
using namespace std;
using namespace llvm;

// Namespace
namespace llvm {
ModulePass *createConstantEncryptionPass(bool flag);
void initializeConstantEncryptionPass(PassRegistry &Registry);
} // namespace llvm
#endif

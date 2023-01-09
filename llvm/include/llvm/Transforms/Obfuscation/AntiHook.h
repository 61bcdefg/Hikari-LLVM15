#ifndef _ANTI_HOOK_H_
#define _ANTI_HOOK_H_
#include "llvm/Pass.h"
using namespace std;
using namespace llvm;

// Namespace
namespace llvm {
ModulePass *createAntiHookPass(bool flag);
void initializeAntiHookPass(PassRegistry &Registry);
} // namespace llvm
#endif

#ifndef _ANTI_DEBUGGING_H_
#define _ANTI_DEBUGGING_H_
#include "llvm/Pass.h"
using namespace std;
using namespace llvm;

// Namespace
namespace llvm {
ModulePass *createAntiDebuggingPass(bool flag);
void initializeAntiDebuggingPass(PassRegistry &Registry);
} // namespace llvm
#endif

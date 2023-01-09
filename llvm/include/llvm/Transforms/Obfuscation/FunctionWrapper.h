#ifndef _FUNCTION_WRAPPER_H_
#define _FUNCTION_WRAPPER_H_
#include "llvm/Pass.h"
using namespace std;
using namespace llvm;

// Namespace
namespace llvm {
ModulePass *createFunctionWrapperPass(bool flag);
void initializeFunctionWrapperPass(PassRegistry &Registry);
} // namespace llvm
#endif

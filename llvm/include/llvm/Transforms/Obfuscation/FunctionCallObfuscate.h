#ifndef _FUNCTION_CALL_OBFUSCATION_H_
#define _FUNCTION_CALL_OBFUSCATION_H_
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
using namespace std;
using namespace llvm;

// Namespace
namespace llvm {
FunctionPass *createFunctionCallObfuscatePass(bool flag);
void initializeFunctionCallObfuscatePass(PassRegistry &Registry);
} // namespace llvm
#endif

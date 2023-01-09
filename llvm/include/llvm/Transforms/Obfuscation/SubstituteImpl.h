#ifndef _SUBSTITUTE_IMPL_H
#define _SUBSTITUTE_IMPL_H
#include "llvm/Transforms/Obfuscation/Substitution.h"

namespace SubstituteImpl {
void substituteAdd(BinaryOperator *bo);
void substituteSub(BinaryOperator *bo);
void substituteAnd(BinaryOperator *bo);
void substituteOr(BinaryOperator *bo);
void substituteXor(BinaryOperator *bo);
void substituteMul(BinaryOperator *bo);
} // namespace SubstituteImpl

#endif

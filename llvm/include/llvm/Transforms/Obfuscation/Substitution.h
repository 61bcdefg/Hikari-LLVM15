//===- SubstitutionIncludes.h - Substitution Obfuscation
//pass-------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains includes and defines for the substitution pass
//
//===----------------------------------------------------------------------===//

#ifndef _SUBSTITUTIONS_H_
#define _SUBSTITUTIONS_H_

// LLVM include
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Obfuscation/CryptoUtils.h"
#if LLVM_VERSION_MAJOR >= 15
#include "llvm/IR/Constants.h"
#endif

// Namespace
using namespace llvm;
using namespace std;

namespace llvm {
FunctionPass *createSubstitutionPass(bool flag);
void initializeSubstitutionPass(PassRegistry &Registry);
} // namespace llvm

#endif

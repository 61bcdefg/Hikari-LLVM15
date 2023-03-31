//===--- RenamedSymbol.cpp - ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactor/RenamedSymbol.h"
#include "clang/AST/DeclObjC.h"
#include <algorithm>

using namespace clang;

namespace clang {
namespace tooling {
namespace rename {

Symbol::Symbol(const NamedDecl *FoundDecl, unsigned SymbolIndex,
               const LangOptions &LangOpts)
    : Name(FoundDecl->getNameAsString(), LangOpts), SymbolIndex(SymbolIndex),
      FoundDecl(FoundDecl) {
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(FoundDecl))
    ObjCSelector = MD->getSelector();
}

bool operator<(const OldSymbolOccurrence &LHS, const OldSymbolOccurrence &RHS) {
  assert(!LHS.Locations.empty() && !RHS.Locations.empty());
  return LHS.Locations[0] < RHS.Locations[0];
}

bool operator==(const OldSymbolOccurrence &LHS,
                const OldSymbolOccurrence &RHS) {
  return LHS.Kind == RHS.Kind && LHS.SymbolIndex == RHS.SymbolIndex &&
         std::equal(LHS.Locations.begin(), LHS.Locations.end(),
                    RHS.Locations.begin());
}

} // end namespace rename
} // end namespace tooling
} // end namespace clang

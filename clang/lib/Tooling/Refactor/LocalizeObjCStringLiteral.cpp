//===--- LocalizeObjCString.cpp -  ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the "Wrap in NSLocalizedString" refactoring operation.
//
//===----------------------------------------------------------------------===//

#include "RefactoringOperations.h"
#include "SourceLocationUtilities.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"

using namespace clang;
using namespace clang::tooling;

namespace {

class LocalizeObjCStringLiteralOperation : public RefactoringOperation {
public:
  LocalizeObjCStringLiteralOperation(const ObjCStringLiteral *E) : E(E) {}

  const Stmt *getTransformedStmt() const override { return E; }

  llvm::Expected<RefactoringResult> perform(ASTContext &Context, const Preprocessor &ThePreprocessor,
          const RefactoringOptionSet &Options,
          unsigned SelectedCandidateIndex) override;

  const ObjCStringLiteral *E;
};

} // end anonymous namespace

RefactoringOperationResult
clang::tooling::initiateLocalizeObjCStringLiteralOperation(
    ASTSlice &Slice, ASTContext &Context, SourceLocation Location,
    SourceRange SelectionRange, bool CreateOperation) {
  const ObjCStringLiteral *E;
  if (SelectionRange.isValid()) {
    auto SelectedSet = Slice.getSelectedStmtSet();
    if (!SelectedSet)
      return None;
    E = dyn_cast_or_null<ObjCStringLiteral>(
        SelectedSet->containsSelectionRange);
  } else
    E = cast_or_null<ObjCStringLiteral>(
        Slice.nearestStmt(Stmt::ObjCStringLiteralClass));
  if (!E)
    return None;

  RefactoringOperationResult Result;
  Result.Initiated = true;
  if (!CreateOperation)
    return Result;
  auto Operation = std::make_unique<LocalizeObjCStringLiteralOperation>(E);
  Result.RefactoringOp = std::move(Operation);
  return Result;
}

llvm::Expected<RefactoringResult>
LocalizeObjCStringLiteralOperation::perform(ASTContext &Context,
                                            const Preprocessor &ThePreprocessor,
                                            const RefactoringOptionSet &Options,
                                            unsigned SelectedCandidateIndex) {
  std::vector<RefactoringReplacement> Replacements;
  // TODO: New API: Replace by something like Node.wrap("NSLocalizedString(", ",
  // @""")
  SourceLocation LocStart =
      Context.getSourceManager().getSpellingLoc(E->getBeginLoc());
  Replacements.emplace_back(SourceRange(LocStart, LocStart),
                            StringRef("NSLocalizedString("));
  SourceLocation LocEnd = getPreciseTokenLocEnd(
      Context.getSourceManager().getSpellingLoc(E->getEndLoc()),
      Context.getSourceManager(), Context.getLangOpts());
  Replacements.emplace_back(SourceRange(LocEnd, LocEnd), StringRef(", @\"\")"));
  return std::move(Replacements);
}

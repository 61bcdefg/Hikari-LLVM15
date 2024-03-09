//===--- FillInMissingProtocolStubs.cpp -  --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the "Add methods from protocol(s)" refactoring operation.
//
//===----------------------------------------------------------------------===//

#include "RefactoringOperations.h"
#include "clang/AST/AST.h"
#include "clang/Edit/RefactoringFixits.h"

using namespace clang;
using namespace clang::tooling;
using namespace edit::fillInMissingProtocolStubs;

namespace {

class FillInMissingProtocolStubsOperation : public RefactoringOperation {
public:
  FillInMissingProtocolStubsOperation(const ObjCContainerDecl *Container,
                                      FillInMissingProtocolStubs Impl)
      : Container(Container), Impl(std::move(Impl)) {}

  const Decl *getTransformedDecl() const override { return Container; }

  llvm::Expected<RefactoringResult> perform(ASTContext &Context, const Preprocessor &ThePreprocessor,
          const RefactoringOptionSet &Options,
          unsigned SelectedCandidateIndex) override;

  const ObjCContainerDecl *Container;
  FillInMissingProtocolStubs Impl;
};

} // end anonymous namespace

RefactoringOperationResult
clang::tooling::initiateFillInMissingProtocolStubsOperation(
    ASTSlice &Slice, ASTContext &Context, SourceLocation Location,
    SourceRange SelectionRange, bool CreateOperation) {
  auto SelectedDecl = Slice.innermostSelectedDecl(
      {Decl::ObjCImplementation, Decl::ObjCCategoryImpl, Decl::ObjCInterface,
       Decl::ObjCCategory},
      ASTSlice::InnermostDeclOnly);
  if (!SelectedDecl)
    return None;
  const auto *Container = cast<ObjCContainerDecl>(SelectedDecl->getDecl());

  // If this in a class extension, initiate the operation on the @implementation
  // if it's in the same TU.
  if (const auto *Category = dyn_cast<ObjCCategoryDecl>(Container)) {
    if (Category->IsClassExtension()) {
      const ObjCInterfaceDecl *I = Category->getClassInterface();
      if (I && I->getImplementation())
        Container = I->getImplementation();
      else
        return RefactoringOperationResult(
            "Class extension without suitable @implementation");
    }
  }

  FillInMissingProtocolStubs Impl;
  if (Impl.initiate(Context, Container))
    return None;
  if (!Impl.hasMissingRequiredMethodStubs())
    return RefactoringOperationResult("All of the @required methods are there");

  RefactoringOperationResult Result;
  Result.Initiated = true;
  if (!CreateOperation)
    return Result;
  auto Operation = std::make_unique<FillInMissingProtocolStubsOperation>(
      Container, std::move(Impl));
  Result.RefactoringOp = std::move(Operation);
  return Result;
}

llvm::Expected<RefactoringResult>
FillInMissingProtocolStubsOperation::perform(
    ASTContext &Context, const Preprocessor &ThePreprocessor,
    const RefactoringOptionSet &Options, unsigned SelectedCandidateIndex) {
  std::vector<RefactoringReplacement> Replacements;
  Impl.perform(Context,
               [&](const FixItHint &Hint) { Replacements.push_back(Hint); });
  return std::move(Replacements);
}

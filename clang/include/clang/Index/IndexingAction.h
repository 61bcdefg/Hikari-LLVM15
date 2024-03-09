//===--- IndexingAction.h - Frontend index action ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INDEX_INDEXINGACTION_H
#define LLVM_CLANG_INDEX_INDEXINGACTION_H

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/LLVM.h"
#include "clang/Index/IndexingOptions.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/ArrayRef.h"
#include <memory>
#include <string>

namespace clang {
  class ASTContext;
  class ASTConsumer;
  class ASTReader;
  class ASTUnit;
  class CompilerInstance;
  class Decl;
  class FrontendAction;
  class FrontendOptions;
  class Module;

namespace serialization {
  class ModuleFile;
}

namespace index {
class IndexDataConsumer;
class IndexUnitWriter;

struct RecordingOptions {
  enum class IncludesRecordingKind {
    None,
    UserOnly, // only record includes inside non-system files.
    All,
  };

  std::string DataDirPath;
  bool RecordSymbolCodeGenName = false;
  bool RecordSystemDependencies = true;
  IncludesRecordingKind RecordIncludes = IncludesRecordingKind::UserOnly;
};
/// Creates an ASTConsumer that indexes all symbols (macros and AST decls).
std::unique_ptr<ASTConsumer>
createIndexingASTConsumer(std::shared_ptr<IndexDataConsumer> DataConsumer,
                          const IndexingOptions &Opts,
                          std::shared_ptr<Preprocessor> PP);

std::unique_ptr<ASTConsumer> createIndexingASTConsumer(
    std::shared_ptr<IndexDataConsumer> DataConsumer,
    const IndexingOptions &Opts, std::shared_ptr<Preprocessor> PP,
    // Prefer to set Opts.ShouldTraverseDecl and use the above overload.
    // This version is only needed if used to *track* function body parsing.
    std::function<bool(const Decl *)> ShouldSkipFunctionBody);

/// Creates a frontend action that indexes all symbols (macros and AST decls).
std::unique_ptr<FrontendAction>
createIndexingAction(std::shared_ptr<IndexDataConsumer> DataConsumer,
                     const IndexingOptions &Opts);

/// Recursively indexes all decls in the AST.
void indexASTUnit(ASTUnit &Unit, IndexDataConsumer &DataConsumer,
                  IndexingOptions Opts);

/// Recursively indexes \p Decls.
void indexTopLevelDecls(ASTContext &Ctx, Preprocessor &PP,
                        ArrayRef<const Decl *> Decls,
                        IndexDataConsumer &DataConsumer, IndexingOptions Opts);

/// Creates a PPCallbacks that indexes macros and feeds macros to \p Consumer.
/// The caller is responsible for calling `Consumer.setPreprocessor()`.
std::unique_ptr<PPCallbacks> indexMacrosCallback(IndexDataConsumer &Consumer,
                                                 IndexingOptions Opts);

/// Recursively indexes all top-level decls in the module.
void indexModuleFile(serialization::ModuleFile &Mod, ASTReader &Reader,
                     IndexDataConsumer &DataConsumer, IndexingOptions Opts);

/// \param WrappedAction another frontend action to wrap over or null.
std::unique_ptr<FrontendAction>
createIndexDataRecordingAction(const FrontendOptions &FEOpts,
                               std::unique_ptr<FrontendAction> WrappedAction);

/// Checks if the unit file exists for the module file, if it doesn't it
/// generates index data for it.
///
/// \returns true if the index data were generated, false otherwise.
bool emitIndexDataForModuleFile(const Module *Mod, const CompilerInstance &CI,
                                IndexUnitWriter &ParentUnitWriter);

} // namespace index
} // namespace clang

#endif

//===--- ScanAndUpdateArgs.h - Util for CC1 Dependency Scanning -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_SCANANDUPDATEARGS_H
#define LLVM_CLANG_DRIVER_SCANANDUPDATEARGS_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {
class StringSaver;
class PrefixMapper;

namespace cas {
class ObjectStore;
class CASID;
} // namespace cas
} // namespace llvm

namespace clang {

class CASOptions;
class CompilerInvocation;
class DiagnosticConsumer;

namespace tooling {
namespace dependencies {
class DependencyScanningTool;

/// Apply CAS inputs for compilation caching to the given invocation, if
/// enabled.
void configureInvocationForCaching(CompilerInvocation &CI, CASOptions CASOpts,
                                   std::string RootID, std::string WorkingDir,
                                   bool ProduceIncludeTree);

struct DepscanPrefixMapping {
  /// Add path mappings to the \p Mapper.
  static void configurePrefixMapper(const CompilerInvocation &Invocation,
                                    llvm::PrefixMapper &Mapper);

  /// Add path mappings to the \p Mapper.
  static void configurePrefixMapper(ArrayRef<std::string> PathPrefixMappings,
                                    llvm::PrefixMapper &Mapper);

  /// Apply the mappings from \p Mapper to \p Invocation.
  static void remapInvocationPaths(CompilerInvocation &Invocation,
                                   llvm::PrefixMapper &Mapper);
};
} // namespace dependencies
} // namespace tooling

Expected<llvm::cas::CASID> scanAndUpdateCC1InlineWithTool(
    tooling::dependencies::DependencyScanningTool &Tool,
    DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS,
    CompilerInvocation &Invocation, StringRef WorkingDirectory,
    llvm::cas::ObjectStore &DB);

} // end namespace clang

#endif

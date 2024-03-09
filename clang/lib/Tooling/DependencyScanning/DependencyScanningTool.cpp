//===- DependencyScanningTool.cpp - clang-scan-deps service ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/DependencyScanning/DependencyScanningTool.h"
#include "CachingActions.h"
#include "clang/CAS/IncludeTree.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/Utils.h"
#include "clang/Tooling/DependencyScanning/ScanAndUpdateArgs.h"
#include "llvm/CAS/ObjectStore.h"

using namespace clang;
using namespace tooling;
using namespace dependencies;
using llvm::Error;

DependencyScanningTool::DependencyScanningTool(
    DependencyScanningService &Service,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
    : Worker(Service, std::move(FS)) {}

namespace {
/// Prints out all of the gathered dependencies into a string.
class MakeDependencyPrinterConsumer : public DependencyConsumer {
public:
  void handleBuildCommand(Command) override {}

  void
  handleDependencyOutputOpts(const DependencyOutputOptions &Opts) override {
    this->Opts = std::make_unique<DependencyOutputOptions>(Opts);
  }

  void handleFileDependency(StringRef File) override {
    Dependencies.push_back(std::string(File));
  }

  // These are ignored for the make format as it can't support the full
  // set of deps, and handleFileDependency handles enough for implicitly
  // built modules to work.
  void handlePrebuiltModuleDependency(PrebuiltModuleDep PMD) override {}
  void handleModuleDependency(ModuleDeps MD) override {}
  void handleDirectModuleDependency(ModuleID ID) override {}
  void handleContextHash(std::string Hash) override {}

  void printDependencies(std::string &S) {
    assert(Opts && "Handled dependency output options.");

    class DependencyPrinter : public DependencyFileGenerator {
    public:
      DependencyPrinter(DependencyOutputOptions &Opts,
                        ArrayRef<std::string> Dependencies)
          : DependencyFileGenerator(Opts) {
        for (const auto &Dep : Dependencies)
          addDependency(Dep);
      }

      void printDependencies(std::string &S) {
        llvm::raw_string_ostream OS(S);
        outputDependencyFile(OS);
      }
    };

    DependencyPrinter Generator(*Opts, Dependencies);
    Generator.printDependencies(S);
  }

protected:
  std::unique_ptr<DependencyOutputOptions> Opts;
  std::vector<std::string> Dependencies;
};
} // anonymous namespace

llvm::Expected<std::string> DependencyScanningTool::getDependencyFile(
    const std::vector<std::string> &CommandLine, StringRef CWD) {
  MakeDependencyPrinterConsumer Consumer;
  CallbackActionController Controller(nullptr);
  auto Result =
      Worker.computeDependencies(CWD, CommandLine, Consumer, Controller);
  if (Result)
    return std::move(Result);
  std::string Output;
  Consumer.printDependencies(Output);
  return Output;
}

namespace {
class EmptyDependencyConsumer : public DependencyConsumer {
  void
  handleDependencyOutputOpts(const DependencyOutputOptions &Opts) override {}

  void handleFileDependency(StringRef Filename) override {}

  void handlePrebuiltModuleDependency(PrebuiltModuleDep PMD) override {}

  void handleModuleDependency(ModuleDeps MD) override {}

  void handleDirectModuleDependency(ModuleID ID) override {}

  void handleContextHash(std::string Hash) override {}
};

/// Returns a CAS tree containing the dependencies.
class GetDependencyTree : public EmptyDependencyConsumer {
public:
  void handleCASFileSystemRootID(std::string ID) override {
    CASFileSystemRootID = ID;
  }

  Expected<llvm::cas::ObjectProxy> getTree() {
    if (CASFileSystemRootID) {
      auto ID = FS.getCAS().parseID(*CASFileSystemRootID);
      if (!ID)
        return ID.takeError();
      return FS.getCAS().getProxy(*ID);
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to get casfs");
  }

  GetDependencyTree(llvm::cas::CachingOnDiskFileSystem &FS) : FS(FS) {}

private:
  llvm::cas::CachingOnDiskFileSystem &FS;
  Optional<std::string> CASFileSystemRootID;
};

/// Returns an IncludeTree containing the dependencies.
class GetIncludeTree : public EmptyDependencyConsumer {
public:
  void handleIncludeTreeID(std::string ID) override { IncludeTreeID = ID; }

  Expected<cas::IncludeTreeRoot> getIncludeTree() {
    if (IncludeTreeID) {
      auto ID = DB.parseID(*IncludeTreeID);
      if (!ID)
        return ID.takeError();
      auto Ref = DB.getReference(*ID);
      if (!Ref)
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(),
            llvm::Twine("missing expected include-tree ") + ID->toString());
      return cas::IncludeTreeRoot::get(DB, *Ref);
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to get include-tree");
  }

  GetIncludeTree(cas::ObjectStore &DB) : DB(DB) {}

private:
  cas::ObjectStore &DB;
  std::optional<std::string> IncludeTreeID;
};
}

llvm::Expected<llvm::cas::ObjectProxy>
DependencyScanningTool::getDependencyTree(
    const std::vector<std::string> &CommandLine, StringRef CWD) {
  GetDependencyTree Consumer(*Worker.getCASFS());
  auto Controller = createCASFSActionController(nullptr, *Worker.getCASFS());
  auto Result =
      Worker.computeDependencies(CWD, CommandLine, Consumer, *Controller);
  if (Result)
    return std::move(Result);
  return Consumer.getTree();
}

llvm::Expected<llvm::cas::ObjectProxy>
DependencyScanningTool::getDependencyTreeFromCompilerInvocation(
    std::shared_ptr<CompilerInvocation> Invocation, StringRef CWD,
    DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS,
    bool DiagGenerationAsCompilation) {
  GetDependencyTree Consumer(*Worker.getCASFS());
  auto Controller = createCASFSActionController(nullptr, *Worker.getCASFS());
  Worker.computeDependenciesFromCompilerInvocation(
      std::move(Invocation), CWD, Consumer, *Controller, DiagsConsumer,
      VerboseOS, DiagGenerationAsCompilation);
  return Consumer.getTree();
}

Expected<cas::IncludeTreeRoot> DependencyScanningTool::getIncludeTree(
    cas::ObjectStore &DB, const std::vector<std::string> &CommandLine,
    StringRef CWD, LookupModuleOutputCallback LookupModuleOutput) {
  GetIncludeTree Consumer(DB);
  auto Controller = createIncludeTreeActionController(LookupModuleOutput, DB);
  llvm::Error Result =
      Worker.computeDependencies(CWD, CommandLine, Consumer, *Controller);
  if (Result)
    return std::move(Result);
  return Consumer.getIncludeTree();
}

Expected<cas::IncludeTreeRoot>
DependencyScanningTool::getIncludeTreeFromCompilerInvocation(
    cas::ObjectStore &DB, std::shared_ptr<CompilerInvocation> Invocation,
    StringRef CWD, LookupModuleOutputCallback LookupModuleOutput,
    DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS,
    bool DiagGenerationAsCompilation) {
  GetIncludeTree Consumer(DB);
  auto Controller = createIncludeTreeActionController(LookupModuleOutput, DB);
  Worker.computeDependenciesFromCompilerInvocation(
      std::move(Invocation), CWD, Consumer, *Controller, DiagsConsumer,
      VerboseOS, DiagGenerationAsCompilation);
  return Consumer.getIncludeTree();
}

llvm::Expected<TranslationUnitDeps>
DependencyScanningTool::getTranslationUnitDependencies(
    const std::vector<std::string> &CommandLine, StringRef CWD,
    const llvm::DenseSet<ModuleID> &AlreadySeen,
    LookupModuleOutputCallback LookupModuleOutput) {
  FullDependencyConsumer Consumer(AlreadySeen);
  auto Controller = createActionController(LookupModuleOutput);
  llvm::Error Result =
      Worker.computeDependencies(CWD, CommandLine, Consumer, *Controller);
  if (Result)
    return std::move(Result);
  return Consumer.takeTranslationUnitDeps();
}

llvm::Expected<ModuleDepsGraph> DependencyScanningTool::getModuleDependencies(
    StringRef ModuleName, const std::vector<std::string> &CommandLine,
    StringRef CWD, const llvm::DenseSet<ModuleID> &AlreadySeen,
    LookupModuleOutputCallback LookupModuleOutput) {
  FullDependencyConsumer Consumer(AlreadySeen);
  auto Controller = createActionController(LookupModuleOutput);
  llvm::Error Result = Worker.computeDependencies(CWD, CommandLine, Consumer,
                                                  *Controller, ModuleName);
  if (Result)
    return std::move(Result);
  return Consumer.takeModuleGraphDeps();
}

TranslationUnitDeps FullDependencyConsumer::takeTranslationUnitDeps() {
  TranslationUnitDeps TU;

  TU.ID.ContextHash = std::move(ContextHash);
  TU.FileDeps = std::move(Dependencies);
  TU.PrebuiltModuleDeps = std::move(PrebuiltModuleDeps);
  TU.Commands = std::move(Commands);
  TU.CASFileSystemRootID = std::move(CASFileSystemRootID);
  TU.IncludeTreeID = std::move(IncludeTreeID);

  for (auto &&M : ClangModuleDeps) {
    auto &MD = M.second;
    // TODO: Avoid handleModuleDependency even being called for modules
    //   we've already seen.
    if (AlreadySeen.count(M.first))
      continue;
    TU.ModuleGraph.push_back(std::move(MD));
  }
  TU.ClangModuleDeps = std::move(DirectModuleDeps);

  return TU;
}

ModuleDepsGraph FullDependencyConsumer::takeModuleGraphDeps() {
  ModuleDepsGraph ModuleGraph;

  for (auto &&M : ClangModuleDeps) {
    auto &MD = M.second;
    // TODO: Avoid handleModuleDependency even being called for modules
    //   we've already seen.
    if (AlreadySeen.count(M.first))
      continue;
    ModuleGraph.push_back(std::move(MD));
  }

  return ModuleGraph;
}

CallbackActionController::~CallbackActionController() {}

std::unique_ptr<DependencyActionController>
DependencyScanningTool::createActionController(
    DependencyScanningWorker &Worker,
    LookupModuleOutputCallback LookupModuleOutput) {
  if (Worker.getScanningFormat() == ScanningOutputFormat::FullIncludeTree)
    return createIncludeTreeActionController(LookupModuleOutput,
                                             *Worker.getCAS());
  if (auto CacheFS = Worker.getCASFS())
    return createCASFSActionController(LookupModuleOutput, *CacheFS);
  return std::make_unique<CallbackActionController>(LookupModuleOutput);
}

std::unique_ptr<DependencyActionController>
DependencyScanningTool::createActionController(
    LookupModuleOutputCallback LookupModuleOutput) {
  return createActionController(Worker, std::move(LookupModuleOutput));
}

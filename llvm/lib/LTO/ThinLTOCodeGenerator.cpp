//===-ThinLTOCodeGenerator.cpp - LLVM Link Time Optimizer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Thin Link Time Optimization library. This library is
// intended to be used by linker to optimize code at link time.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/legacy/ThinLTOCodeGenerator.h"
#include "llvm/CAS/ActionCache.h"
#include "llvm/CAS/ObjectStore.h"
#include "llvm/RemoteCachingService/Client.h"
#include "llvm/Support/CommandLine.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/LTO/LTO.h"
#include "llvm/LTO/SummaryBasedOptimizations.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Remarks/HotnessThresholdParser.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/ScopedDurationTimer.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/Transforms/ObjCARC.h"
#include "llvm/Transforms/Utils/FunctionImportUtils.h"

#include <memory>
#include <numeric>
#include <queue>

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

using namespace llvm;

#define DEBUG_TYPE "thinlto"

namespace llvm {
// Flags -discard-value-names, defined in LTOCodeGenerator.cpp
extern cl::opt<bool> LTODiscardValueNames;
extern cl::opt<std::string> RemarksFilename;
extern cl::opt<std::string> RemarksPasses;
extern cl::opt<bool> RemarksWithHotness;
extern cl::opt<std::optional<uint64_t>, false, remarks::HotnessThresholdParser>
    RemarksHotnessThreshold;
extern cl::opt<std::string> RemarksFormat;
}

namespace {

// Default to using all available threads in the system, but using only one
// thred per core, as indicated by the usage of
// heavyweight_hardware_concurrency() below.
static cl::opt<int> ThreadCount("threads", cl::init(0));
static cl::opt<bool> CacheLogging(
    "thinlto-cache-logging", cl::desc("Enable logging for thinLTO caching"),
    cl::init((bool)sys::Process::GetEnv("LLVM_THINLTO_CACHE_LOGGING")),
    cl::Hidden);
static cl::opt<bool> DeterministicCheck(
    "thinlto-deterministic-check",
    cl::desc("Enable deterministic check for thinLTO caching"),
    cl::init((bool)sys::Process::GetEnv(
        "LLVM_CACHE_CHECK_REPRODUCIBLE_CACHING_ISSUES")),
    cl::Hidden);
static cl::opt<bool> StrictCASErrors(
    "thinlto-strict-cas-errors",
    cl::desc("Treat CAS errors during ThinLTO as fatal errors"),
    cl::init((bool)sys::Process::GetEnv("LLVM_THINLTO_STRICT_CAS_ERRORS")),
    cl::Hidden);

class LoggingStream {
public:
  LoggingStream(raw_ostream &OS) : OS(OS) {}
  void applyLocked(llvm::function_ref<void(raw_ostream &OS)> Fn) {
    std::unique_lock<std::mutex> LockGuard(Lock);
    auto Now = std::chrono::system_clock::now();
    OS << Now << ": ";
    Fn(OS);
    OS.flush();
  }

private:
  std::mutex Lock;
  raw_ostream &OS;
};

// Simple helper to save temporary files for debug.
static void saveTempBitcode(const Module &TheModule, StringRef TempDir,
                            unsigned count, StringRef Suffix) {
  if (TempDir.empty())
    return;
  // User asked to save temps, let dump the bitcode file after import.
  std::string SaveTempPath = (TempDir + llvm::Twine(count) + Suffix).str();
  std::error_code EC;
  raw_fd_ostream OS(SaveTempPath, EC, sys::fs::OF_None);
  if (EC)
    report_fatal_error(Twine("Failed to open ") + SaveTempPath +
                       " to save optimized bitcode\n");
  WriteBitcodeToFile(TheModule, OS, /* ShouldPreserveUseListOrder */ true);
}

static const GlobalValueSummary *
getFirstDefinitionForLinker(const GlobalValueSummaryList &GVSummaryList) {
  // If there is any strong definition anywhere, get it.
  auto StrongDefForLinker = llvm::find_if(
      GVSummaryList, [](const std::unique_ptr<GlobalValueSummary> &Summary) {
        auto Linkage = Summary->linkage();
        return !GlobalValue::isAvailableExternallyLinkage(Linkage) &&
               !GlobalValue::isWeakForLinker(Linkage);
      });
  if (StrongDefForLinker != GVSummaryList.end())
    return StrongDefForLinker->get();
  // Get the first *linker visible* definition for this global in the summary
  // list.
  auto FirstDefForLinker = llvm::find_if(
      GVSummaryList, [](const std::unique_ptr<GlobalValueSummary> &Summary) {
        auto Linkage = Summary->linkage();
        return !GlobalValue::isAvailableExternallyLinkage(Linkage);
      });
  // Extern templates can be emitted as available_externally.
  if (FirstDefForLinker == GVSummaryList.end())
    return nullptr;
  return FirstDefForLinker->get();
}

// Populate map of GUID to the prevailing copy for any multiply defined
// symbols. Currently assume first copy is prevailing, or any strong
// definition. Can be refined with Linker information in the future.
static void computePrevailingCopies(
    const ModuleSummaryIndex &Index,
    DenseMap<GlobalValue::GUID, const GlobalValueSummary *> &PrevailingCopy) {
  auto HasMultipleCopies = [&](const GlobalValueSummaryList &GVSummaryList) {
    return GVSummaryList.size() > 1;
  };

  for (auto &I : Index) {
    if (HasMultipleCopies(I.second.SummaryList))
      PrevailingCopy[I.first] =
          getFirstDefinitionForLinker(I.second.SummaryList);
  }
}

static StringMap<lto::InputFile *>
generateModuleMap(std::vector<std::unique_ptr<lto::InputFile>> &Modules) {
  StringMap<lto::InputFile *> ModuleMap;
  for (auto &M : Modules) {
    LLVM_DEBUG(dbgs() << "Adding module " << M->getName() << " to ModuleMap\n");
    assert(!ModuleMap.contains(M->getName()) &&
           "Expect unique Buffer Identifier");
    ModuleMap[M->getName()] = M.get();
  }
  return ModuleMap;
}

static void promoteModule(Module &TheModule, const ModuleSummaryIndex &Index,
                          bool ClearDSOLocalOnDeclarations) {
  if (renameModuleForThinLTO(TheModule, Index, ClearDSOLocalOnDeclarations))
    report_fatal_error("renameModuleForThinLTO failed");
}

namespace {
class ThinLTODiagnosticInfo : public DiagnosticInfo {
  const Twine &Msg;
public:
  ThinLTODiagnosticInfo(const Twine &DiagMsg,
                        DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_Linker, Severity), Msg(DiagMsg) {}
  void print(DiagnosticPrinter &DP) const override { DP << Msg; }
};
}

/// Verify the module and strip broken debug info.
static void verifyLoadedModule(Module &TheModule) {
  bool BrokenDebugInfo = false;
  if (verifyModule(TheModule, &dbgs(), &BrokenDebugInfo))
    report_fatal_error("Broken module found, compilation aborted!");
  if (BrokenDebugInfo) {
    TheModule.getContext().diagnose(ThinLTODiagnosticInfo(
        "Invalid debug info found, debug info will be stripped", DS_Warning));
    StripDebugInfo(TheModule);
  }
}

static std::unique_ptr<Module> loadModuleFromInput(lto::InputFile *Input,
                                                   LLVMContext &Context,
                                                   bool Lazy,
                                                   bool IsImporting) {
  auto &Mod = Input->getSingleBitcodeModule();
  SMDiagnostic Err;
  Expected<std::unique_ptr<Module>> ModuleOrErr =
      Lazy ? Mod.getLazyModule(Context,
                               /* ShouldLazyLoadMetadata */ true, IsImporting)
           : Mod.parseModule(Context);
  if (!ModuleOrErr) {
    handleAllErrors(ModuleOrErr.takeError(), [&](ErrorInfoBase &EIB) {
      SMDiagnostic Err = SMDiagnostic(Mod.getModuleIdentifier(),
                                      SourceMgr::DK_Error, EIB.message());
      Err.print("ThinLTO", errs());
    });
    report_fatal_error("Can't load module, abort.");
  }
  if (!Lazy)
    verifyLoadedModule(*ModuleOrErr.get());
  return std::move(*ModuleOrErr);
}

static void
crossImportIntoModule(Module &TheModule, const ModuleSummaryIndex &Index,
                      StringMap<lto::InputFile *> &ModuleMap,
                      const FunctionImporter::ImportMapTy &ImportList,
                      bool ClearDSOLocalOnDeclarations) {
  auto Loader = [&](StringRef Identifier) {
    auto &Input = ModuleMap[Identifier];
    return loadModuleFromInput(Input, TheModule.getContext(),
                               /*Lazy=*/true, /*IsImporting*/ true);
  };

  FunctionImporter Importer(Index, Loader, ClearDSOLocalOnDeclarations);
  Expected<bool> Result = Importer.importFunctions(TheModule, ImportList);
  if (!Result) {
    handleAllErrors(Result.takeError(), [&](ErrorInfoBase &EIB) {
      SMDiagnostic Err = SMDiagnostic(TheModule.getModuleIdentifier(),
                                      SourceMgr::DK_Error, EIB.message());
      Err.print("ThinLTO", errs());
    });
    report_fatal_error("importFunctions failed");
  }
  // Verify again after cross-importing.
  verifyLoadedModule(TheModule);
}

static void optimizeModule(Module &TheModule, TargetMachine &TM,
                           unsigned OptLevel, bool Freestanding,
                           bool DebugPassManager, ModuleSummaryIndex *Index) {
  std::optional<PGOOptions> PGOOpt;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassInstrumentationCallbacks PIC;
  StandardInstrumentations SI(TheModule.getContext(), DebugPassManager);
  SI.registerCallbacks(PIC, &MAM);
  PipelineTuningOptions PTO;
  PTO.LoopVectorization = true;
  PTO.SLPVectorization = true;
  PassBuilder PB(&TM, PTO, PGOOpt, &PIC);

  std::unique_ptr<TargetLibraryInfoImpl> TLII(
      new TargetLibraryInfoImpl(Triple(TM.getTargetTriple())));
  if (Freestanding)
    TLII->disableAllFunctions();
  FAM.registerPass([&] { return TargetLibraryAnalysis(*TLII); });

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;

  OptimizationLevel OL;

  switch (OptLevel) {
  default:
    llvm_unreachable("Invalid optimization level");
  case 0:
    OL = OptimizationLevel::O0;
    break;
  case 1:
    OL = OptimizationLevel::O1;
    break;
  case 2:
    OL = OptimizationLevel::O2;
    break;
  case 3:
    OL = OptimizationLevel::O3;
    break;
  }

  MPM.addPass(PB.buildThinLTODefaultPipeline(OL, Index));

  MPM.run(TheModule, MAM);
}

static void
addUsedSymbolToPreservedGUID(const lto::InputFile &File,
                             DenseSet<GlobalValue::GUID> &PreservedGUID) {
  for (const auto &Sym : File.symbols()) {
    if (Sym.isUsed())
      PreservedGUID.insert(GlobalValue::getGUID(Sym.getIRName()));
  }
}

// Convert the PreservedSymbols map from "Name" based to "GUID" based.
static void computeGUIDPreservedSymbols(const lto::InputFile &File,
                                        const StringSet<> &PreservedSymbols,
                                        const Triple &TheTriple,
                                        DenseSet<GlobalValue::GUID> &GUIDs) {
  // Iterate the symbols in the input file and if the input has preserved symbol
  // compute the GUID for the symbol.
  for (const auto &Sym : File.symbols()) {
    if (PreservedSymbols.count(Sym.getName()) && !Sym.getIRName().empty())
      GUIDs.insert(GlobalValue::getGUID(GlobalValue::getGlobalIdentifier(
          Sym.getIRName(), GlobalValue::ExternalLinkage, "")));
  }
}

static DenseSet<GlobalValue::GUID>
computeGUIDPreservedSymbols(const lto::InputFile &File,
                            const StringSet<> &PreservedSymbols,
                            const Triple &TheTriple) {
  DenseSet<GlobalValue::GUID> GUIDPreservedSymbols(PreservedSymbols.size());
  computeGUIDPreservedSymbols(File, PreservedSymbols, TheTriple,
                              GUIDPreservedSymbols);
  return GUIDPreservedSymbols;
}

std::unique_ptr<MemoryBuffer> codegenModule(Module &TheModule,
                                            TargetMachine &TM) {
  SmallVector<char, 128> OutputBuffer;

  // CodeGen
  {
    raw_svector_ostream OS(OutputBuffer);
    legacy::PassManager PM;

    // If the bitcode files contain ARC code and were compiled with optimization,
    // the ObjCARCContractPass must be run, so do it unconditionally here.
    PM.add(createObjCARCContractPass());

    // Setup the codegen now.
    if (TM.addPassesToEmitFile(PM, OS, nullptr, CodeGenFileType::ObjectFile,
                               /* DisableVerify */ true))
      report_fatal_error("Failed to setup codegen");

    // Run codegen now. resulting binary is in OutputBuffer.
    PM.run(TheModule);
  }
  return std::make_unique<SmallVectorMemoryBuffer>(
      std::move(OutputBuffer), /*RequiresNullTerminator=*/false);
}

struct NullModuleCacheEntry : ModuleCacheEntry {
  std::string getEntryPath() override { return "<null>"; }
  ErrorOr<std::unique_ptr<MemoryBuffer>> tryLoadingBuffer() override {
    return std::error_code();
  }
  void write(const MemoryBuffer &OutputBuffer) override {}
};

class FileModuleCacheEntry : public ModuleCacheEntry {
public:
  static std::unique_ptr<ModuleCacheEntry> create(StringRef CachePath,
                                                  std::string Key) {
    if (CachePath.empty())
      return std::make_unique<NullModuleCacheEntry>();
    return std::make_unique<FileModuleCacheEntry>(CachePath, std::move(Key));
  }

  // Create a cache entry. This compute a unique hash for the Module considering
  // the current list of export/import, and offer an interface to query to
  // access the content in the cache.
  FileModuleCacheEntry(StringRef CachePath, std::string Key) {
    assert(!CachePath.empty());
    // This choice of file name allows the cache to be pruned (see pruneCache()
    // in include/llvm/Support/CachePruning.h).
    sys::path::append(EntryPath, CachePath, "llvmcache-" + Key);
  }

  std::string getEntryPath() final { return EntryPath.str().str(); }

  // Try loading the buffer for this cache entry.
  ErrorOr<std::unique_ptr<MemoryBuffer>> tryLoadingBuffer() final {
    return MemoryBuffer::getFile(EntryPath);
  }

  // Cache the Produced object file
  void write(const MemoryBuffer &OutputBuffer) final {
    if (auto Err = llvm::writeToOutput(
            EntryPath, [&OutputBuffer](llvm::raw_ostream &OS) -> llvm::Error {
              OS << OutputBuffer.getBuffer();
              return llvm::Error::success();
            }))
      report_fatal_error(llvm::formatv("ThinLTO: Can't write file {0}: {1}",
                                       EntryPath,
                                       toString(std::move(Err)).c_str()));
  }

  Error writeObject(const MemoryBuffer &OutputBuffer,
                    StringRef OutputPath) final {
    // Clear output file if exists for hard-linking.
    sys::fs::remove(OutputPath);
    // Hard-link the entry (or copy if hard-link fails).
    auto Err = sys::fs::create_hard_link(EntryPath, OutputPath);
    if (!Err)
      return Error::success();
    // Hard linking failed, try to copy.
    Err = sys::fs::copy_file(EntryPath, OutputPath);
    if (!Err)
      return Error::success();
    // Copy failed (could be because the CacheEntry was removed from the cache
    // in the meantime by another process), fall back and try to write down
    // the buffer to the output.
    errs() << "remark: can't link or copy from cached entry '" << EntryPath
           << "' to '" << OutputPath << "'\n";
    // Fallback to default.
    return ModuleCacheEntry::writeObject(OutputBuffer, OutputPath);
  }

  std::optional<std::unique_ptr<MemoryBuffer>> getMappedBuffer() final {
    auto ReloadedBufferOrErr = tryLoadingBuffer();
    if (auto EC = ReloadedBufferOrErr.getError()) {
      // On error, keep the preexisting buffer and print a diagnostic.
      errs() << "remark: can't reload cached file '" << getEntryPath()
             << "': " << EC.message() << "\n";
    }
    return std::move(*ReloadedBufferOrErr);
  }

private:
  SmallString<128> EntryPath;
};

struct CancellationToken {
  std::atomic<bool> WantsToExit{false};
  std::mutex Mutex;
  std::condition_variable CondVar;
  unsigned ExitBlockers{0};

  void requestCancellation() {
    WantsToExit = true;
    std::unique_lock Lock(Mutex);
    CondVar.wait(Lock, [&]() { return ExitBlockers == 0; });
  }

  [[nodiscard]] auto acquireHandle() {
    auto ReleaseHandleOnScopeExit = llvm::make_scope_exit([this]() {
      std::unique_lock Lock(Mutex);
      --ExitBlockers;
      CondVar.notify_one();
    });

    using RetTy = std::optional<decltype(ReleaseHandleOnScopeExit)>;

    if (WantsToExit) {
      ReleaseHandleOnScopeExit.release();
      return RetTy{};
    }

    std::unique_lock Lock(Mutex);
    ++ExitBlockers;

    return RetTy{std::move(ReleaseHandleOnScopeExit)};
  }
};

static void handleCASError(
    Error E, llvm::function_ref<void(llvm::function_ref<void(raw_ostream &OS)>)>
                 Logger) {
  if (!E)
    return;

  // If strict CAS error, abort.
  if (StrictCASErrors)
    report_fatal_error(std::move(E));

  // Otherwise, log the error message and return error_code.
  if (Logger)
    Logger([&](raw_ostream &OS) {
      OS << "LTO CAS Error: '" << toString(std::move(E)) << "'\n";
    });
  else
    consumeError(std::move(E));
}

static cas::CASID createCASProxyOrAbort(cas::ObjectStore &CAS, StringRef Key) {
  // Create the key by inserting cache key (SHA1) into CAS to create an ID for
  // the correct context.
  // TODO: We can have an alternative hashing function that doesn't need to
  // store the key into CAS to get the CacheKey.
  auto CASKey = CAS.createProxy(std::nullopt, Key);
  if (!CASKey)
    report_fatal_error(CASKey.takeError());
  return CASKey->getID();
}

// ThinLTOCacheEntry: manage caching for a single Module.
class AsyncModuleCacheEntry {
public:
  // Access the path to this entry in the cache.
  virtual std::string getEntryPath() = 0;

  /// Attempt to asynchronously load the cached buffer and invoke the callback.
  /// Cache miss is represented as std::error_code().
  virtual void tryLoadingBuffer(
      std::function<void(ErrorOr<std::unique_ptr<MemoryBuffer>>)> Cb) = 0;

  /// Attempt to asynchronously write the computed buffer and invoke the
  /// callback.
  virtual void write(const MemoryBuffer &OutputBuffer,
                     std::function<void()> Cb) = 0;

  virtual Error writeObject(const MemoryBuffer &OutputBuffer,
                            StringRef OutputPath) {
    std::error_code Err;
    raw_fd_ostream OS(OutputPath, Err, sys::fs::CD_CreateAlways);
    if (Err)
      return createStringError(Err, Twine("Can't open output '") + OutputPath);
    OS << OutputBuffer.getBuffer();
    return Error::success();
  }

  virtual std::optional<std::unique_ptr<MemoryBuffer>> getMappedBuffer() {
    return std::nullopt;
  }

  /// Check whether the loaded and written results of this entry are identical.
  /// This is only called when DeterministicCheck is enabled, and after both
  /// \c tryLoadingBuffer() and \c write() have finished.
  virtual bool areLoadedAndWrittenResultsIdentical() const { return true; }

  virtual ~AsyncModuleCacheEntry() {}
};

struct NullAsyncModuleCacheEntry : AsyncModuleCacheEntry {
  std::string getEntryPath() override { return "<null>"; }

  void tryLoadingBuffer(
      std::function<void(ErrorOr<std::unique_ptr<MemoryBuffer>>)> Cb) override {
    return Cb(std::error_code());
  }

  void write(const MemoryBuffer &OutputBuffer,
             std::function<void()> Cb) override {
    return Cb();
  }
};

class CASModuleCacheEntry : public AsyncModuleCacheEntry {
public:
  // Create a cache entry. This compute a unique hash for the Module considering
  // the current list of export/import, and offer an interface to query to
  // access the content in the cache.
  CASModuleCacheEntry(
      cas::ObjectStore &CAS, cas::ActionCache &Cache, std::string Key,
      std::function<void(llvm::function_ref<void(raw_ostream &OS)>)> Logger)
      : CAS(CAS), Cache(Cache), ID(createCASProxyOrAbort(CAS, Key)),
        Logger(std::move(Logger)) {}

  std::string getEntryPath() final {
    return ID.toString();
  }

  void tryLoadingBuffer(
      std::function<void(ErrorOr<std::unique_ptr<MemoryBuffer>>)> Cb) final {
    auto LookupStart = std::chrono::steady_clock::now();
    Cache.getAsync(ID, /*Globally=*/true, [=](auto KeyID) {
      if (Logger) {
        auto LookupEnd = std::chrono::steady_clock::now();
        auto Seconds =
            std::chrono::duration<double>(LookupEnd - LookupStart).count();
        Logger([&](raw_ostream &OS) {
          OS << "LTO cache lookup '" << ID << "' in "
             << llvm::format("%.6fs", Seconds) << "\n";
        });
      }

      std::optional<cas::CASID> MaybeKeyID;
      if (auto E = std::move(KeyID).moveInto(MaybeKeyID)) {
        handleCASError(std::move(E), Logger);
        // If handleCASError didn't abort, treat as miss.
        return Cb(std::error_code());
      }

      if (!MaybeKeyID)
        return Cb(std::error_code());

      auto LoadStart = std::chrono::steady_clock::now();
      CAS.getProxyAsync(*MaybeKeyID, [=](auto MaybeObject) {
        if (Logger) {
          auto LoadEnd = std::chrono::steady_clock::now();
          auto Seconds =
              std::chrono::duration<double>(LoadEnd - LoadStart).count();
          Logger([&](raw_ostream &OS) {
            OS << "LTO cache load '" << ID << "' in "
               << llvm::format("%.6fs", Seconds) << "\n";
          });
        }

        if (!MaybeObject) {
          handleCASError(MaybeObject.takeError(), Logger);
          // If handleCASError didn't abort, treat as miss.
          return Cb(std::error_code());
        }
        return Cb((*MaybeObject)->getMemoryBuffer("", /*NullTerminated=*/true));
      });
    });
  }

  void write(const MemoryBuffer &OutputBuffer, std::function<void()> Cb) final {
    std::optional<cas::ObjectProxy> Proxy;
    if (Error E =
            CAS.createProxy({}, OutputBuffer.getBuffer()).moveInto(Proxy)) {
      handleCASError(std::move(E), Logger);
      return Cb();
    }

    auto UpdateStart = std::chrono::steady_clock::now();
    Cache.putAsync(ID, Proxy->getID(), /*Globally=*/true, [=](auto Err) {
      if (Logger) {
        auto UpdateEnd = std::chrono::steady_clock::now();
        auto Seconds =
            std::chrono::duration<double>(UpdateEnd - UpdateStart).count();
        Logger([&](raw_ostream &OS) {
          OS << "LTO cache update '" << ID << "' in "
             << llvm::format("%.6fs", Seconds) << "\n";
        });
      }

      if (Err)
        report_fatal_error(std::move(Err));
      Cb();
    });
  }

private:
  cas::ObjectStore &CAS;
  cas::ActionCache &Cache;
  cas::CASID ID;
  std::function<void(llvm::function_ref<void(raw_ostream &OS)>)> Logger;
};

class RemoteModuleCacheEntry : public AsyncModuleCacheEntry {
public:
  // Create a cache entry. This compute a unique hash for the Module considering
  // the current list of export/import, and offer an interface to query to
  // access the content in the cache.
  RemoteModuleCacheEntry(
      std::shared_ptr<CancellationToken> GetCancelTok,
      std::shared_ptr<CancellationToken> PutCancelTok,
      cas::remote::ClientServices &Service,
      StringRef OutputPath, std::string Key,
      std::function<void(llvm::function_ref<void(raw_ostream &OS)>)> Logger)
      : GetCancelTok(GetCancelTok), PutCancelTok(PutCancelTok), Service(Service), ID(std::move(Key)),
        OutputPath(OutputPath.str()), Logger(std::move(Logger)) {}

  std::string getEntryPath() final { return ID; }

  void tryLoadingBuffer(
      std::function<void(ErrorOr<std::unique_ptr<MemoryBuffer>>)> Cb) final {
    auto LookupStart = std::chrono::steady_clock::now();
    Service.KVDB->getValueAsync(ID, [=, GetCancelTok = GetCancelTok](auto ErrOrGetResponse) {
      auto CancelTokHandle = GetCancelTok->acquireHandle();
      if (!CancelTokHandle) {
        (void)expectedToOptional(std::move(ErrOrGetResponse));
        return;
      }

      if (Logger) {
        auto LookupEnd = std::chrono::steady_clock::now();
        auto Seconds =
            std::chrono::duration<double>(LookupEnd - LookupStart).count();
        Logger([&](raw_ostream &OS) {
          OS << "LTO cache lookup '" << ID << "' in "
             << llvm::format("%.6fs", Seconds) << "\n";
        });
      }

      // Use the KVDB to map the ThinLTO key onto the CAS ID.
      std::optional<cas::remote::KeyValueDBClient::ValueTy> GetResponse;
      if (Error E = std::move(ErrOrGetResponse).moveInto(GetResponse)) {
        handleCASError(std::move(E), Logger);
        // If handleCASError didn't abort, treat as miss.
        return Cb(std::error_code());
      }

      // Cache Miss.
      if (!GetResponse)
        return Cb(std::error_code());

      // Malformed output. Error.
      auto Result = GetResponse->find("Output");
      if (Result == GetResponse->end())
        return Cb(std::make_error_code(std::errc::message_size));

      LoadedCASID = Result->getValue();

      std::string TmpPath = OutputPath + ".downloaded.tmp";

      // Request the output buffer.
      auto LoadStart = std::chrono::steady_clock::now();
      Service.CASDB->loadAsync(
          *LoadedCASID, TmpPath, [=, GetCancelTok = GetCancelTok](auto LoadResponse) {
            auto CancelTokHandle = GetCancelTok->acquireHandle();
            if (!CancelTokHandle) {
              (void)expectedToOptional(std::move(LoadResponse));
              return;
            }

            if (Logger) {
              auto LoadEnd = std::chrono::steady_clock::now();
              auto Seconds =
                  std::chrono::duration<double>(LoadEnd - LoadStart).count();
              Logger([&](raw_ostream &OS) {
                OS << "LTO cache load '" << ID << "' in "
                   << llvm::format("%.6fs", Seconds) << "\n";
              });
            }

            if (!LoadResponse) {
              handleCASError(LoadResponse.takeError(), Logger);
              // If handleCASError didn't abort, treat as miss.
              return Cb(std::error_code());
            }

            // Object not found. Treat it as a miss.
            if (LoadResponse->KeyNotFound)
              return Cb(std::error_code());

            sys::fs::rename(TmpPath, OutputPath);
            // Note: This might pick up the file created by rename in \c
            // write().
            return Cb(MemoryBuffer::getFile(OutputPath));
          });
    });
  }

  Error writeObject(const MemoryBuffer &OutputBuffer,
                    StringRef OutputPath) final {
    std::string TmpPath = this->OutputPath + ".computed.tmp";

    if (Error E = AsyncModuleCacheEntry::writeObject(OutputBuffer, TmpPath))
      return E;

    if (auto EC = sys::fs::rename(TmpPath, OutputPath)) {
      (void)sys::fs::remove(TmpPath); // Attempt to clean up.
      return createStringError(
          EC, "renaming of computed module object file failed");
    }

    return Error::success();
  }

  void write(const MemoryBuffer &OutputBuffer, std::function<void()> Cb) final {
    std::string TmpPath = OutputPath + ".uploaded.tmp";

    if (Error E = AsyncModuleCacheEntry::writeObject(OutputBuffer, TmpPath)) {
      handleCASError(std::move(E), Logger);
      return;
    }

    auto SaveStart = std::chrono::steady_clock::now();
    // FIXME: Consider passing the buffer contents this in a string instead of file.
    Service.CASDB->saveFileAsync(TmpPath, [=, PutCancelTok = PutCancelTok](auto MaybeWrittenCASID) {
      sys::fs::remove(TmpPath);

      auto CancelTokHandle = PutCancelTok->acquireHandle();
      if (!CancelTokHandle) {
        (void)expectedToOptional(std::move(MaybeWrittenCASID));
        return;
      }

      if (Logger) {
        auto SaveEnd = std::chrono::steady_clock::now();
        auto Seconds =
            std::chrono::duration<double>(SaveEnd - SaveStart).count();
        Logger([&](raw_ostream &OS) {
          OS << "LTO cache save '" << ID << "' in "
             << llvm::format("%.6fs", Seconds) << "\n";
        });
      }

      if (Error E = std::move(MaybeWrittenCASID).moveInto(WrittenCASID)) {
        handleCASError(std::move(E), Logger);
        return Cb();
      }

      cas::remote::KeyValueDBClient::ValueTy CompResult;
      CompResult["Output"] = *WrittenCASID;

      auto UpdateStart = std::chrono::steady_clock::now();
      Service.KVDB->putValueAsync(ID, CompResult, [=, PutCancelTok = PutCancelTok](auto Err) {
        auto CancelTokHandle = PutCancelTok->acquireHandle();
        if (!CancelTokHandle) {
          (void)consumeError(std::move(Err));
          return;
        }

        if (Logger) {
          auto UpdateEnd = std::chrono::steady_clock::now();
          auto Seconds =
              std::chrono::duration<double>(UpdateEnd - UpdateStart).count();
          Logger([&](raw_ostream &OS) {
            OS << "LTO cache update '" << ID << "' in "
               << llvm::format("%.6fs", Seconds) << "\n";
          });
        }

        if (Err)
          handleCASError(std::move(Err), Logger);

        return Cb();
      });
    });
  }

  bool areLoadedAndWrittenResultsIdentical() const override {
    if (LoadedCASID && WrittenCASID && *LoadedCASID != *WrittenCASID) {
      Error E = createStringError(
          inconvertibleErrorCode(),
          "ThinLTO deterministic check failed: " + *LoadedCASID +
              " (expected) vs. " + *WrittenCASID + " (actual)");
      handleCASError(std::move(E), Logger);
      return false;
    }
    return true;
  }

private:
  std::shared_ptr<CancellationToken> GetCancelTok;
  std::shared_ptr<CancellationToken> PutCancelTok;
  cas::remote::ClientServices &Service;
  std::string ID;
  std::string OutputPath;
  std::optional<std::string> LoadedCASID;
  std::optional<std::string> WrittenCASID;
  std::function<void(llvm::function_ref<void(raw_ostream &OS)>)> Logger;
};

static std::unique_ptr<MemoryBuffer>
ProcessThinLTOModule(Module &TheModule, ModuleSummaryIndex &Index,
                     StringMap<lto::InputFile *> &ModuleMap, TargetMachine &TM,
                     const FunctionImporter::ImportMapTy &ImportList,
                     const FunctionImporter::ExportSetTy &ExportList,
                     const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
                     const GVSummaryMapTy &DefinedGlobals,
                     const ThinLTOCodeGenerator::CachingOptions &CacheOptions,
                     bool DisableCodeGen, StringRef SaveTempsDir,
                     bool Freestanding, unsigned OptLevel, unsigned count,
                     bool DebugPassManager) {
  // "Benchmark"-like optimization: single-source case
  bool SingleModule = (ModuleMap.size() == 1);

  // When linking an ELF shared object, dso_local should be dropped. We
  // conservatively do this for -fpic.
  bool ClearDSOLocalOnDeclarations =
      TM.getTargetTriple().isOSBinFormatELF() &&
      TM.getRelocationModel() != Reloc::Static &&
      TheModule.getPIELevel() == PIELevel::Default;

  if (!SingleModule) {
    promoteModule(TheModule, Index, ClearDSOLocalOnDeclarations);

    // Apply summary-based prevailing-symbol resolution decisions.
    thinLTOFinalizeInModule(TheModule, DefinedGlobals, /*PropagateAttrs=*/true);

    // Save temps: after promotion.
    saveTempBitcode(TheModule, SaveTempsDir, count, ".1.promoted.bc");
  }

  // Be friendly and don't nuke totally the module when the client didn't
  // supply anything to preserve.
  if (!ExportList.empty() || !GUIDPreservedSymbols.empty()) {
    // Apply summary-based internalization decisions.
    thinLTOInternalizeModule(TheModule, DefinedGlobals);
  }

  // Save internalized bitcode
  saveTempBitcode(TheModule, SaveTempsDir, count, ".2.internalized.bc");

  if (!SingleModule)
    crossImportIntoModule(TheModule, Index, ModuleMap, ImportList,
                          ClearDSOLocalOnDeclarations);

  // Do this after any importing so that imported code is updated.
  // See comment at call to updateVCallVisibilityInIndex() for why
  // WholeProgramVisibilityEnabledInLTO is false.
  updatePublicTypeTestCalls(TheModule,
                            /* WholeProgramVisibilityEnabledInLTO */ false);

  // Save temps: after cross-module import.
  saveTempBitcode(TheModule, SaveTempsDir, count, ".3.imported.bc");

  optimizeModule(TheModule, TM, OptLevel, Freestanding, DebugPassManager,
                 &Index);

  saveTempBitcode(TheModule, SaveTempsDir, count, ".4.opt.bc");

  if (DisableCodeGen) {
    // Configured to stop before CodeGen, serialize the bitcode and return.
    SmallVector<char, 128> OutputBuffer;
    {
      raw_svector_ostream OS(OutputBuffer);
      ProfileSummaryInfo PSI(TheModule);
      auto Index = buildModuleSummaryIndex(TheModule, nullptr, &PSI);
      WriteBitcodeToFile(TheModule, OS, true, &Index);
    }
    return std::make_unique<SmallVectorMemoryBuffer>(
        std::move(OutputBuffer), /*RequiresNullTerminator=*/false);
  }

  return codegenModule(TheModule, TM);
}

/// Resolve prevailing symbols. Record resolutions in the \p ResolvedODR map
/// for caching, and in the \p Index for application during the ThinLTO
/// backends. This is needed for correctness for exported symbols (ensure
/// at least one copy kept) and a compile-time optimization (to drop duplicate
/// copies when possible).
static void resolvePrevailingInIndex(
    ModuleSummaryIndex &Index,
    StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>>
        &ResolvedODR,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
    const DenseMap<GlobalValue::GUID, const GlobalValueSummary *>
        &PrevailingCopy) {

  auto isPrevailing = [&](GlobalValue::GUID GUID, const GlobalValueSummary *S) {
    const auto &Prevailing = PrevailingCopy.find(GUID);
    // Not in map means that there was only one copy, which must be prevailing.
    if (Prevailing == PrevailingCopy.end())
      return true;
    return Prevailing->second == S;
  };

  auto recordNewLinkage = [&](StringRef ModuleIdentifier,
                              GlobalValue::GUID GUID,
                              GlobalValue::LinkageTypes NewLinkage) {
    ResolvedODR[ModuleIdentifier][GUID] = NewLinkage;
  };

  // TODO Conf.VisibilityScheme can be lto::Config::ELF for ELF.
  lto::Config Conf;
  thinLTOResolvePrevailingInIndex(Conf, Index, isPrevailing, recordNewLinkage,
                                  GUIDPreservedSymbols);
}

// Initialize the TargetMachine builder for a given Triple
static void initTMBuilder(TargetMachineBuilder &TMBuilder,
                          const Triple &TheTriple) {
  if (TMBuilder.MCpu.empty())
    TMBuilder.MCpu = lto::getThinLTODefaultCPU(TheTriple);
  TMBuilder.TheTriple = std::move(TheTriple);
}

} // end anonymous namespace

std::optional<std::string> ModuleCacheEntry::computeCacheKey(
    const ModuleSummaryIndex &Index, StringRef ModuleID,
    const FunctionImporter::ImportMapTy &ImportList,
    const FunctionImporter::ExportSetTy &ExportList,
    const std::map<GlobalValue::GUID, GlobalValue::LinkageTypes> &ResolvedODR,
    const GVSummaryMapTy &DefinedGVSummaries, unsigned OptLevel,
    bool Freestanding, const TargetMachineBuilder &TMBuilder) {
  if (!Index.modulePaths().count(ModuleID))
    // The module does not have an entry, it can't have a hash at all
    return std::nullopt;

  if (all_of(Index.getModuleHash(ModuleID), [](uint32_t V) { return V == 0; }))
    // No hash entry, no caching!
    return std::nullopt;

  llvm::lto::Config Conf;
  Conf.OptLevel = OptLevel;
  Conf.Options = TMBuilder.Options;
  Conf.CPU = TMBuilder.MCpu;
  Conf.MAttrs.push_back(TMBuilder.MAttr);
  Conf.RelocModel = TMBuilder.RelocModel;
  Conf.CGOptLevel = TMBuilder.CGOptLevel;
  Conf.Freestanding = Freestanding;
  SmallString<40> Key;
  computeLTOCacheKey(Key, Conf, Index, ModuleID, ImportList, ExportList,
                     ResolvedODR, DefinedGVSummaries);

  return Key.str().str();
}

Error ModuleCacheEntry::writeObject(const MemoryBuffer &OutputBuffer,
                                    StringRef OutputPath) {
  std::error_code Err;
  raw_fd_ostream OS(OutputPath, Err, sys::fs::CD_CreateAlways);
  if (Err)
    return createStringError(Err, Twine("Can't open output '") + OutputPath);
  OS << OutputBuffer.getBuffer();
  return Error::success();
}

Error ThinLTOCodeGenerator::setCacheDir(std::string Path) {
  // CacheDir can only be set once.
  if (!CacheOptions.Path.empty())
    return Error::success();

  StringRef PathStr = Path;
  // The environment overwrites the option parameter.
  if (PathStr.consume_front("cas:")) {
    CacheOptions.Type = CachingOptions::CacheType::CAS;
    // Create ObjectStore and ActionCache.
    auto MaybeCAS = cas::createOnDiskCAS(PathStr);
    if (!MaybeCAS)
      return MaybeCAS.takeError();
    CacheOptions.CAS = std::move(*MaybeCAS);
    auto MaybeCache = cas::createOnDiskActionCache(PathStr);
    if (!MaybeCache)
      return MaybeCache.takeError();
    CacheOptions.Cache = std::move(*MaybeCache);
    CacheOptions.Path = PathStr.str();
  } else if (PathStr.consume_front("grpc:")) {
    CacheOptions.Type = CachingOptions::CacheType::RemoteService;
    auto MaybeService =
        cas::remote::createCompilationCachingRemoteClient(PathStr);
    if (!MaybeService)
      return MaybeService.takeError();
    CacheOptions.Service = std::move(*MaybeService);
    CacheOptions.Path = PathStr.str();
  } else {
    CacheOptions.Type = CachingOptions::CacheType::CacheDirectory;
    CacheOptions.Path = std::move(Path);
  }

  return Error::success();
}

std::unique_ptr<ModuleCacheEntry> ThinLTOCodeGenerator::createModuleCacheEntry(
    const ModuleSummaryIndex &Index, StringRef ModuleID, StringRef OutputPath,
    const FunctionImporter::ImportMapTy &ImportList,
    const FunctionImporter::ExportSetTy &ExportList,
    const std::map<GlobalValue::GUID, GlobalValue::LinkageTypes> &ResolvedODR,
    const GVSummaryMapTy &DefinedGVSummaries, unsigned OptLevel,
    bool Freestanding, const TargetMachineBuilder &TMBuilder,
    std::function<void(llvm::function_ref<void(raw_ostream &OS)>)> Logger) {
  assert(CacheOptions.Type == CachingOptions::CacheType::CacheDirectory);

  std::optional<std::string> Key = ModuleCacheEntry::computeCacheKey(
      Index, ModuleID, ImportList, ExportList, ResolvedODR, DefinedGVSummaries,
      OptLevel, Freestanding, TMBuilder);

  if (!Key)
    return std::make_unique<NullModuleCacheEntry>();

  return FileModuleCacheEntry::create(CacheOptions.Path, std::move(*Key));
}

static std::unique_ptr<AsyncModuleCacheEntry> createAsyncModuleCacheEntry(
    std::shared_ptr<CancellationToken> GetCancelTok,
    std::shared_ptr<CancellationToken> PutCancelTok,
    ThinLTOCodeGenerator::CachingOptions &CacheOptions,
    const ModuleSummaryIndex &Index, StringRef ModuleID, StringRef OutputPath,
    const FunctionImporter::ImportMapTy &ImportList,
    const FunctionImporter::ExportSetTy &ExportList,
    const std::map<GlobalValue::GUID, GlobalValue::LinkageTypes> &ResolvedODR,
    const GVSummaryMapTy &DefinedGVSummaries, unsigned OptLevel,
    bool Freestanding, const TargetMachineBuilder &TMBuilder,
    std::function<void(llvm::function_ref<void(raw_ostream &OS)>)> Logger) {
  std::optional<std::string> Key = ModuleCacheEntry::computeCacheKey(
      Index, ModuleID, ImportList, ExportList, ResolvedODR, DefinedGVSummaries,
      OptLevel, Freestanding, TMBuilder);

  if (!Key)
    return std::make_unique<NullAsyncModuleCacheEntry>();

  switch (CacheOptions.Type) {
  case ThinLTOCodeGenerator::CachingOptions::CacheType::CAS:
    return std::make_unique<CASModuleCacheEntry>(
        *CacheOptions.CAS, *CacheOptions.Cache, std::move(*Key),
        std::move(Logger));
  case ThinLTOCodeGenerator::CachingOptions::CacheType::RemoteService:
    return std::make_unique<RemoteModuleCacheEntry>(
        GetCancelTok, PutCancelTok, *CacheOptions.Service, OutputPath,
        std::move(*Key), std::move(Logger));
  case ThinLTOCodeGenerator::CachingOptions::CacheType::CacheDirectory:
    llvm_unreachable("creating async module cache entry in sync context");
  }
}

void ThinLTOCodeGenerator::addModule(StringRef Identifier, StringRef Data) {
  MemoryBufferRef Buffer(Data, Identifier);

  auto InputOrError = lto::InputFile::create(Buffer);
  if (!InputOrError)
    report_fatal_error(Twine("ThinLTO cannot create input file: ") +
                       toString(InputOrError.takeError()));

  auto TripleStr = (*InputOrError)->getTargetTriple();
  Triple TheTriple(TripleStr);

  if (Modules.empty())
    initTMBuilder(TMBuilder, Triple(TheTriple));
  else if (TMBuilder.TheTriple != TheTriple) {
    if (!TMBuilder.TheTriple.isCompatibleWith(TheTriple))
      report_fatal_error("ThinLTO modules with incompatible triples not "
                         "supported");
    initTMBuilder(TMBuilder, Triple(TMBuilder.TheTriple.merge(TheTriple)));
  }

  Modules.emplace_back(std::move(*InputOrError));
}

void ThinLTOCodeGenerator::preserveSymbol(StringRef Name) {
  PreservedSymbols.insert(Name);
}

void ThinLTOCodeGenerator::crossReferenceSymbol(StringRef Name) {
  // FIXME: At the moment, we don't take advantage of this extra information,
  // we're conservatively considering cross-references as preserved.
  //  CrossReferencedSymbols.insert(Name);
  PreservedSymbols.insert(Name);
}

// TargetMachine factory
std::unique_ptr<TargetMachine> TargetMachineBuilder::create() const {
  std::string ErrMsg;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TheTriple.str(), ErrMsg);
  if (!TheTarget) {
    report_fatal_error(Twine("Can't load target for this Triple: ") + ErrMsg);
  }

  // Use MAttr as the default set of features.
  SubtargetFeatures Features(MAttr);
  Features.getDefaultSubtargetFeatures(TheTriple);
  std::string FeatureStr = Features.getString();

  std::unique_ptr<TargetMachine> TM(
      TheTarget->createTargetMachine(TheTriple.str(), MCpu, FeatureStr, Options,
                                     RelocModel, std::nullopt, CGOptLevel));
  assert(TM && "Cannot create target machine");

  return TM;
}

/**
 * Produce the combined summary index from all the bitcode files:
 * "thin-link".
 */
std::unique_ptr<ModuleSummaryIndex> ThinLTOCodeGenerator::linkCombinedIndex() {
  std::unique_ptr<ModuleSummaryIndex> CombinedIndex =
      std::make_unique<ModuleSummaryIndex>(/*HaveGVs=*/false);
  for (auto &Mod : Modules) {
    auto &M = Mod->getSingleBitcodeModule();
    if (Error Err = M.readSummary(*CombinedIndex, Mod->getName())) {
      // FIXME diagnose
      logAllUnhandledErrors(
          std::move(Err), errs(),
          "error: can't create module summary index for buffer: ");
      return nullptr;
    }
  }
  return CombinedIndex;
}

namespace {
struct IsExported {
  const DenseMap<StringRef, FunctionImporter::ExportSetTy> &ExportLists;
  const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols;

  IsExported(
      const DenseMap<StringRef, FunctionImporter::ExportSetTy> &ExportLists,
      const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols)
      : ExportLists(ExportLists), GUIDPreservedSymbols(GUIDPreservedSymbols) {}

  bool operator()(StringRef ModuleIdentifier, ValueInfo VI) const {
    const auto &ExportList = ExportLists.find(ModuleIdentifier);
    return (ExportList != ExportLists.end() && ExportList->second.count(VI)) ||
           GUIDPreservedSymbols.count(VI.getGUID());
  }
};

struct IsPrevailing {
  const DenseMap<GlobalValue::GUID, const GlobalValueSummary *> &PrevailingCopy;
  IsPrevailing(const DenseMap<GlobalValue::GUID, const GlobalValueSummary *>
                   &PrevailingCopy)
      : PrevailingCopy(PrevailingCopy) {}

  bool operator()(GlobalValue::GUID GUID, const GlobalValueSummary *S) const {
    const auto &Prevailing = PrevailingCopy.find(GUID);
    // Not in map means that there was only one copy, which must be prevailing.
    if (Prevailing == PrevailingCopy.end())
      return true;
    return Prevailing->second == S;
  };
};
} // namespace

static void computeDeadSymbolsInIndex(
    ModuleSummaryIndex &Index,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols) {
  // We have no symbols resolution available. And can't do any better now in the
  // case where the prevailing symbol is in a native object. It can be refined
  // with linker information in the future.
  auto isPrevailing = [&](GlobalValue::GUID G) {
    return PrevailingType::Unknown;
  };
  computeDeadSymbolsWithConstProp(Index, GUIDPreservedSymbols, isPrevailing,
                                  /* ImportEnabled = */ true);
}

static std::string computeThinLTOOutputPath(unsigned count,
                                            StringRef SavedObjectsDirectoryPath,
                                            TargetMachineBuilder &TMBuilder) {
  auto ArchName = TMBuilder.TheTriple.getArchName();
  SmallString<128> OutputPath(SavedObjectsDirectoryPath);
  llvm::sys::path::append(OutputPath,
                          Twine(count) + "." + ArchName + ".thinlto.o");
  return OutputPath.c_str(); // Ensure the string is null terminated.
}

/**
 * Perform promotion and renaming of exported internal functions.
 * Index is updated to reflect linkage changes from weak resolution.
 */
void ThinLTOCodeGenerator::promote(Module &TheModule, ModuleSummaryIndex &Index,
                                   const lto::InputFile &File) {
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries;
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      File, PreservedSymbols, Triple(TheModule.getTargetTriple()));

  // Add used symbol to the preserved symbols.
  addUsedSymbolToPreservedGUID(File, GUIDPreservedSymbols);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  // Generate import/export list
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);

  // Resolve prevailing symbols
  StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>> ResolvedODR;
  resolvePrevailingInIndex(Index, ResolvedODR, GUIDPreservedSymbols,
                           PrevailingCopy);

  thinLTOFinalizeInModule(TheModule,
                          ModuleToDefinedGVSummaries[ModuleIdentifier],
                          /*PropagateAttrs=*/false);

  // Promote the exported values in the index, so that they are promoted
  // in the module.
  thinLTOInternalizeAndPromoteInIndex(
      Index, IsExported(ExportLists, GUIDPreservedSymbols),
      IsPrevailing(PrevailingCopy));

  // FIXME Set ClearDSOLocalOnDeclarations.
  promoteModule(TheModule, Index, /*ClearDSOLocalOnDeclarations=*/false);
}

/**
 * Perform cross-module importing for the module identified by ModuleIdentifier.
 */
void ThinLTOCodeGenerator::crossModuleImport(Module &TheModule,
                                             ModuleSummaryIndex &Index,
                                             const lto::InputFile &File) {
  auto ModuleMap = generateModuleMap(Modules);
  auto ModuleCount = Index.modulePaths().size();

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      File, PreservedSymbols, Triple(TheModule.getTargetTriple()));

  addUsedSymbolToPreservedGUID(File, GUIDPreservedSymbols);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  // Generate import/export list
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);
  auto &ImportList = ImportLists[TheModule.getModuleIdentifier()];

  // FIXME Set ClearDSOLocalOnDeclarations.
  crossImportIntoModule(TheModule, Index, ModuleMap, ImportList,
                        /*ClearDSOLocalOnDeclarations=*/false);
}

/**
 * Compute the list of summaries needed for importing into module.
 */
void ThinLTOCodeGenerator::gatherImportedSummariesForModule(
    Module &TheModule, ModuleSummaryIndex &Index,
    std::map<std::string, GVSummaryMapTy> &ModuleToSummariesForIndex,
    const lto::InputFile &File) {
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      File, PreservedSymbols, Triple(TheModule.getTargetTriple()));

  addUsedSymbolToPreservedGUID(File, GUIDPreservedSymbols);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  // Generate import/export list
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);

  llvm::gatherImportedSummariesForModule(
      ModuleIdentifier, ModuleToDefinedGVSummaries,
      ImportLists[ModuleIdentifier], ModuleToSummariesForIndex);
}

/**
 * Emit the list of files needed for importing into module.
 */
void ThinLTOCodeGenerator::emitImports(Module &TheModule, StringRef OutputName,
                                       ModuleSummaryIndex &Index,
                                       const lto::InputFile &File) {
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      File, PreservedSymbols, Triple(TheModule.getTargetTriple()));

  addUsedSymbolToPreservedGUID(File, GUIDPreservedSymbols);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  // Generate import/export list
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);

  std::map<std::string, GVSummaryMapTy> ModuleToSummariesForIndex;
  llvm::gatherImportedSummariesForModule(
      ModuleIdentifier, ModuleToDefinedGVSummaries,
      ImportLists[ModuleIdentifier], ModuleToSummariesForIndex);

  std::error_code EC;
  if ((EC = EmitImportsFiles(ModuleIdentifier, OutputName,
                             ModuleToSummariesForIndex)))
    report_fatal_error(Twine("Failed to open ") + OutputName +
                       " to save imports lists\n");
}

/**
 * Perform internalization. Runs promote and internalization together.
 * Index is updated to reflect linkage changes.
 */
void ThinLTOCodeGenerator::internalize(Module &TheModule,
                                       ModuleSummaryIndex &Index,
                                       const lto::InputFile &File) {
  initTMBuilder(TMBuilder, Triple(TheModule.getTargetTriple()));
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols =
      computeGUIDPreservedSymbols(File, PreservedSymbols, TMBuilder.TheTriple);

  addUsedSymbolToPreservedGUID(File, GUIDPreservedSymbols);

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  // Generate import/export list
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);
  auto &ExportList = ExportLists[ModuleIdentifier];

  // Be friendly and don't nuke totally the module when the client didn't
  // supply anything to preserve.
  if (ExportList.empty() && GUIDPreservedSymbols.empty())
    return;

  // Resolve prevailing symbols
  StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>> ResolvedODR;
  resolvePrevailingInIndex(Index, ResolvedODR, GUIDPreservedSymbols,
                           PrevailingCopy);

  // Promote the exported values in the index, so that they are promoted
  // in the module.
  thinLTOInternalizeAndPromoteInIndex(
      Index, IsExported(ExportLists, GUIDPreservedSymbols),
      IsPrevailing(PrevailingCopy));

  // FIXME Set ClearDSOLocalOnDeclarations.
  promoteModule(TheModule, Index, /*ClearDSOLocalOnDeclarations=*/false);

  // Internalization
  thinLTOFinalizeInModule(TheModule,
                          ModuleToDefinedGVSummaries[ModuleIdentifier],
                          /*PropagateAttrs=*/false);

  thinLTOInternalizeModule(TheModule,
                           ModuleToDefinedGVSummaries[ModuleIdentifier]);
}

/**
 * Perform post-importing ThinLTO optimizations.
 */
void ThinLTOCodeGenerator::optimize(Module &TheModule) {
  initTMBuilder(TMBuilder, Triple(TheModule.getTargetTriple()));

  // Optimize now
  optimizeModule(TheModule, *TMBuilder.create(), OptLevel, Freestanding,
                 DebugPassManager, nullptr);
}

/// Write out the generated object file, either from CacheEntryPath or from
/// OutputBuffer, preferring hard-link when possible.
/// Returns the path to the generated file in SavedObjectsDirectoryPath.
std::string
ThinLTOCodeGenerator::writeGeneratedObject(StringRef OutputPath,
                                           ModuleCacheEntry *CacheEntry,
                                           const MemoryBuffer &OutputBuffer) {
  // We don't return a memory buffer to the linker, just a list of files.
  if (CacheEntry) {
    Error Err = CacheEntry->writeObject(OutputBuffer, OutputPath);
    if (Err)
      report_fatal_error(std::move(Err));
    return std::string(OutputPath);
  }
  // No cache entry, just write out the buffer.
  std::error_code Err;
  raw_fd_ostream OS(OutputPath, Err, sys::fs::CD_CreateAlways);
  if (Err)
    report_fatal_error(Twine("Can't open output '") + OutputPath + "'\n");
  OS << OutputBuffer.getBuffer();
  return std::string(OutputPath);
}

template <class T>
class Awaitable {
  T Value;
  std::mutex Mutex;
  std::condition_variable Channel;

public:
  explicit Awaitable(T Value) : Value(std::move(Value)) {}

  bool check(llvm::function_ref<bool(const T &)> Cond) {
    std::lock_guard Lock{Mutex};
    return Cond(Value);
  }

  void wait(llvm::function_ref<bool(const T &)> Cond) {
    std::unique_lock Lock{Mutex};
    if (Cond(Value))
      return;

    Channel.wait(Lock, [&]() { return Cond(Value); });
  }

  template <class Duration>
  bool waitFor(Duration MaxDuration, llvm::function_ref<bool(const T &)> Cond) {
    std::unique_lock Lock{Mutex};
    if (Cond(Value))
      return true;

    return Channel.wait_for(Lock, MaxDuration, [&]() { return Cond(Value); });
  }

  T apply(llvm::function_ref<void(T &)> Modifier) {
    T Result = [&]() {
      std::lock_guard Lock{Mutex};
      Modifier(Value);
      return Value;
    }();
    Channel.notify_one();
    return Result;
  }
};

// Main entry point for the ThinLTO processing
void ThinLTOCodeGenerator::run() {
  timeTraceProfilerBegin("ThinLink", StringRef(""));
  auto TimeTraceScopeExit = llvm::make_scope_exit([]() {
    if (llvm::timeTraceProfilerEnabled())
      llvm::timeTraceProfilerEnd();
  });
  // Prepare the resulting object vector
  assert(ProducedBinaries.empty() && "The generator should not be reused");

  // When using RemoteService caching, we will always create a saved object
  // directory for remote service to pass back the cached object file.
  // First, we need to remember whether the caller requests buffer API or file
  // API based on if the SavedObjectsDirectoryPath was set or not.
  bool UseBufferAPI = SavedObjectsDirectoryPath.empty();
  std::string TempDirectory;
  if (CacheOptions.Type == CachingOptions::CacheType::RemoteService &&
      SavedObjectsDirectoryPath.empty()) {
    SmallString<128> TempPath;
    std::error_code EC = llvm::sys::fs::createUniqueDirectory("temp", TempPath);
    if (EC)
      report_fatal_error("cannot create temp directory");
    SavedObjectsDirectoryPath = TempPath.c_str();
    TempDirectory = SavedObjectsDirectoryPath;
  }

  if (UseBufferAPI)
    ProducedBinaries.resize(Modules.size());

  if (!SavedObjectsDirectoryPath.empty()) {
    sys::fs::create_directories(SavedObjectsDirectoryPath);
    bool IsDir;
    sys::fs::is_directory(SavedObjectsDirectoryPath, IsDir);
    if (!IsDir)
      report_fatal_error(Twine("Unexistent dir: '") + SavedObjectsDirectoryPath + "'");
    ProducedBinaryFiles.resize(Modules.size());
  }

  auto CleanTempDirAtExit = make_scope_exit([&]() {
    if (!TempDirectory.empty())
      llvm::sys::fs::remove_directories(TempDirectory);
  });

  if (CodeGenOnly) {
    // Perform only parallel codegen and return.
    DefaultThreadPool Pool;
    int count = 0;
    for (auto &Mod : Modules) {
      Pool.async([&](int count) {
        LLVMContext Context;
        Context.setDiscardValueNames(LTODiscardValueNames);

        std::string OutputPath = computeThinLTOOutputPath(
            count, SavedObjectsDirectoryPath, TMBuilder);

        // Parse module now
        auto TheModule = loadModuleFromInput(Mod.get(), Context, false,
                                             /*IsImporting*/ false);

        // CodeGen
        auto OutputBuffer = codegenModule(*TheModule, *TMBuilder.create());
        if (UseBufferAPI)
          ProducedBinaries[count] = std::move(OutputBuffer);
        else
          ProducedBinaryFiles[count] =
              writeGeneratedObject(OutputPath, nullptr, *OutputBuffer);
      }, count++);
    }

    return;
  }

  // Sequential linking phase
  auto Index = linkCombinedIndex();

  // Save temps: index.
  if (!SaveTempsDir.empty()) {
    auto SaveTempPath = SaveTempsDir + "index.bc";
    std::error_code EC;
    raw_fd_ostream OS(SaveTempPath, EC, sys::fs::OF_None);
    if (EC)
      report_fatal_error(Twine("Failed to open ") + SaveTempPath +
                         " to save optimized bitcode\n");
    writeIndexToFile(*Index, OS);
  }


  // Prepare the module map.
  auto ModuleMap = generateModuleMap(Modules);
  auto ModuleCount = Modules.size();

  // Collect for each module the list of function it defines (GUID -> Summary).
  DenseMap<StringRef, GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index->collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID, this is needed for
  // computing the caching hash and the internalization.
  DenseSet<GlobalValue::GUID> GUIDPreservedSymbols;
  for (const auto &M : Modules)
    computeGUIDPreservedSymbols(*M, PreservedSymbols, TMBuilder.TheTriple,
                                GUIDPreservedSymbols);

  // Add used symbol from inputs to the preserved symbols.
  for (const auto &M : Modules)
    addUsedSymbolToPreservedGUID(*M, GUIDPreservedSymbols);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(*Index, GUIDPreservedSymbols);

  // Synthesize entry counts for functions in the combined index.
  computeSyntheticCounts(*Index);

  // Currently there is no support for enabling whole program visibility via a
  // linker option in the old LTO API, but this call allows it to be specified
  // via the internal option. Must be done before WPD below.
  if (hasWholeProgramVisibility(/* WholeProgramVisibilityEnabledInLTO */ false))
    Index->setWithWholeProgramVisibility();

  // FIXME: This needs linker information via a TBD new interface
  updateVCallVisibilityInIndex(*Index,
                               /*WholeProgramVisibilityEnabledInLTO=*/false,
                               // FIXME: These need linker information via a
                               // TBD new interface.
                               /*DynamicExportSymbols=*/{},
                               /*VisibleToRegularObjSymbols=*/{});

  // Perform index-based WPD. This will return immediately if there are
  // no index entries in the typeIdMetadata map (e.g. if we are instead
  // performing IR-based WPD in hybrid regular/thin LTO mode).
  std::map<ValueInfo, std::vector<VTableSlotSummary>> LocalWPDTargetsMap;
  std::set<GlobalValue::GUID> ExportedGUIDs;
  runWholeProgramDevirtOnIndex(*Index, ExportedGUIDs, LocalWPDTargetsMap);
  for (auto GUID : ExportedGUIDs)
    GUIDPreservedSymbols.insert(GUID);

  // Compute prevailing symbols
  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(*Index, PrevailingCopy);

  // Collect the import/export lists for all modules from the call-graph in the
  // combined index.
  DenseMap<StringRef, FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  DenseMap<StringRef, FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(*Index, ModuleToDefinedGVSummaries,
                           IsPrevailing(PrevailingCopy), ImportLists,
                           ExportLists);

  // We use a std::map here to be able to have a defined ordering when
  // producing a hash for the cache entry.
  // FIXME: we should be able to compute the caching hash for the entry based
  // on the index, and nuke this map.
  StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>> ResolvedODR;

  // Resolve prevailing symbols, this has to be computed early because it
  // impacts the caching.
  resolvePrevailingInIndex(*Index, ResolvedODR, GUIDPreservedSymbols,
                           PrevailingCopy);

  // Use global summary-based analysis to identify symbols that can be
  // internalized (because they aren't exported or preserved as per callback).
  // Changes are made in the index, consumed in the ThinLTO backends.
  updateIndexWPDForExports(*Index,
                           IsExported(ExportLists, GUIDPreservedSymbols),
                           LocalWPDTargetsMap);
  thinLTOInternalizeAndPromoteInIndex(
      *Index, IsExported(ExportLists, GUIDPreservedSymbols),
      IsPrevailing(PrevailingCopy));

  thinLTOPropagateFunctionAttrs(*Index, IsPrevailing(PrevailingCopy));

  // Make sure that every module has an entry in the ExportLists, ImportList,
  // GVSummary and ResolvedODR maps to enable threaded access to these maps
  // below.
  for (auto &Module : Modules) {
    auto ModuleIdentifier = Module->getName();
    ExportLists[ModuleIdentifier];
    ImportLists[ModuleIdentifier];
    ResolvedODR[ModuleIdentifier];
    ModuleToDefinedGVSummaries[ModuleIdentifier];
  }

  std::vector<BitcodeModule *> ModulesVec;
  ModulesVec.reserve(Modules.size());
  for (auto &Mod : Modules)
    ModulesVec.push_back(&Mod->getSingleBitcodeModule());
  std::vector<int> ModulesOrdering = lto::generateModulesOrdering(ModulesVec);

  if (llvm::timeTraceProfilerEnabled())
    llvm::timeTraceProfilerEnd();

  TimeTraceScopeExit.release();
  LoggingStream CacheLogOS(llvm::errs());

  // We can try running cache queries asynchronously.
  if (CacheOptions.Type == CachingOptions::CacheType::CAS ||
      CacheOptions.Type == CachingOptions::CacheType::RemoteService) {
    enum ModuleState {
      // Initial state, no guarantees.
      MS_Empty = 0b0000,
      // Computation is done, ComputedBuffer is valid.
      MS_ComputationDone = 0b0001,
      // Cache lookup is done, no other guarantees.
      MS_CacheLookupDone = 0b0010,
      // Cache lookup is done, CachedBuffer is valid.
      MS_CacheLookupHit = 0b0100,
    };
    struct ModuleInfo {
      std::unique_ptr<AsyncModuleCacheEntry> Entry;
      std::atomic<unsigned> State = MS_Empty;
      std::unique_ptr<MemoryBuffer> ComputedBuffer;
      std::unique_ptr<MemoryBuffer> CachedBuffer;
    };
    std::vector<ModuleInfo> Infos(ModuleCount);

    auto GetCancelTok = std::make_shared<CancellationToken>();
    auto PutCancelTok = std::make_shared<CancellationToken>();

    Awaitable<std::size_t> WrittenObjects{0};
    Awaitable<std::size_t> HandledCacheReads{0};
    Awaitable<std::size_t> HandledCacheWrites{0};
    auto AllModules = [ModuleCount](std::size_t V) { return V == ModuleCount; };
    auto Increment = [](std::size_t &V) { ++V; };

    auto WriteObject = [&](int count, bool CacheHit) {
      auto &Buffer =
          CacheHit ? Infos[count].CachedBuffer : Infos[count].ComputedBuffer;

      if (UseBufferAPI) {
        ProducedBinaries[count] = std::move(Buffer);
      } else {
        std::string OutputPath = computeThinLTOOutputPath(
            count, SavedObjectsDirectoryPath, TMBuilder);

        Error Err = Infos[count].Entry->writeObject(*Buffer, OutputPath);
        if (Err)
          report_fatal_error(std::move(Err));
        ProducedBinaryFiles[count] = std::string(OutputPath);
      }

      WrittenObjects.apply(Increment);
    };

    auto WriteCache = [&, PutCancelTok](int count) {
      if (count == -1) {
        HandledCacheWrites.apply(Increment);
        return;
      }

      auto &OutputBuffer =
          UseBufferAPI ? ProducedBinaries[count] : Infos[count].ComputedBuffer;

      // Commit to the cache.
      Infos[count].Entry->write(*OutputBuffer, [&, PutCancelTok]() {
        auto CancelTokHandle = PutCancelTok->acquireHandle();
        if (!CancelTokHandle)
          return;

        // TODO: With UseBufferAPI, we could try replacing ProducedBinaries[count]
        //  with the buffer that's now cached on disk.

        HandledCacheWrites.apply(Increment);
      });
    };

    std::mutex WriteQueueMutex;
    std::queue<int> WriteQueue;
    auto EnqueueCacheWrite = [&](int count) {
      std::scoped_lock Lock(WriteQueueMutex);
      WriteQueue.push(count);
      if (HandledCacheReads.check(AllModules)) {
        while (!WriteQueue.empty()) {
          WriteCache(WriteQueue.front());
          WriteQueue.pop();
        }
      }
    };

    // Query the cache.
    for (auto IndexCount : ModulesOrdering) {
      auto &Mod = Modules[IndexCount];
      auto ModuleIdentifier = Mod->getName();

      std::string OutputPath = computeThinLTOOutputPath(
          IndexCount, SavedObjectsDirectoryPath, TMBuilder);

      auto &CacheEntry = Infos[IndexCount].Entry = createAsyncModuleCacheEntry(
          GetCancelTok, PutCancelTok, CacheOptions, *Index, ModuleIdentifier,
          OutputPath, ImportLists[ModuleIdentifier],
          ExportLists[ModuleIdentifier], ResolvedODR[ModuleIdentifier],
          ModuleToDefinedGVSummaries[ModuleIdentifier], OptLevel, Freestanding,
          TMBuilder,
          [&CacheLogOS](llvm::function_ref<void(raw_ostream & OS)> Log) {
            if (CacheLogging)
              CacheLogOS.applyLocked(Log);
            else {
              raw_null_ostream OS;
              Log(OS);
            }
          });

      CacheEntry->tryLoadingBuffer([&, IndexCount,
                                    GetCancelTok](auto ErrOrBuffer) {
        auto CancelTokHandle = GetCancelTok->acquireHandle();
        if (!CancelTokHandle)
          return;

        bool CacheHit{ErrOrBuffer};
        unsigned CacheBits = MS_CacheLookupDone;

        if (CacheHit) {
          Infos[IndexCount].CachedBuffer = std::move(ErrOrBuffer.get());
          CacheBits |= MS_CacheLookupHit;
        }

        unsigned ComputationState = Infos[IndexCount].State.fetch_or(CacheBits);

        // Consider writing the object file.
        if (DeterministicCheck) {
          // With deterministic checks, writing the object file is always
          // responsibility of the compute task.
        } else {
          // Without deterministic checks, writing the object file is
          // responsibility of whoever comes up with usable result first.
          if (!(ComputationState & MS_ComputationDone) && CacheHit)
            WriteObject(IndexCount, /*CacheHit=*/true);
        }

        HandledCacheReads.apply(Increment);

        // Enqueueing cache write is responsibility of whoever lost the race.
        if (DeterministicCheck) {
          if (ComputationState & MS_ComputationDone)
            EnqueueCacheWrite(IndexCount);
        } else {
          if (ComputationState & MS_ComputationDone) {
            if (!CacheHit)
              EnqueueCacheWrite(IndexCount);
            else
              EnqueueCacheWrite(-1);
          }
        }
      });
    }

    // Parallel optimizer + codegen
    {
      DefaultThreadPool Pool(heavyweight_hardware_concurrency(ThreadCount));
      for (auto count : ModulesOrdering) {
        auto ModuleEntryPath = Infos[count].Entry->getEntryPath();
        Pool.async([&, count, ModuleEntryPath, GetCancelTok] {
          auto CancelTokHandle = GetCancelTok->acquireHandle();
          if (!CancelTokHandle) {
            if (CacheLogging)
              CacheLogOS.applyLocked([&](raw_ostream &OS) {
                OS << "LTO compute '" << ModuleEntryPath
                   << "' skipped due to completion\n";
              });
            return;
          }

          auto &Mod = Modules[count];
          StringRef ModuleIdentifier = Mod->getName();

          if (!DeterministicCheck) {
            unsigned CacheState = Infos[count].State;
            if (CacheState & MS_CacheLookupHit) {
              if (CacheLogging)
                CacheLogOS.applyLocked([&](raw_ostream &OS) {
                  OS << "LTO compute '" << Infos[count].Entry->getEntryPath()
                     << "' skipped due to cache hit\n";
                });
              EnqueueCacheWrite(-1);
              return;
            }
          }

          if (CacheLogging)
            CacheLogOS.applyLocked([&](raw_ostream &OS) {
              OS << "LTO compute '" << Infos[count].Entry->getEntryPath()
                 << "' started\n";
            });
          ScopedDurationTimer T([&](double Seconds) {
            if (CacheLogging)
              CacheLogOS.applyLocked([&](raw_ostream &OS) {
                OS << "LTO compute '" << Infos[count].Entry->getEntryPath()
                   << "' in " << llvm::format("%.6fs", Seconds) << "\n";
              });
          });

          LLVMContext Context;
          Context.setDiscardValueNames(LTODiscardValueNames);
          Context.enableDebugTypeODRUniquing();
          auto DiagFileOrErr = lto::setupLLVMOptimizationRemarks(
              Context, RemarksFilename, RemarksPasses, RemarksFormat,
              RemarksWithHotness, RemarksHotnessThreshold, count);
          if (!DiagFileOrErr) {
            errs() << "Error: " << toString(DiagFileOrErr.takeError()) << "\n";
            report_fatal_error("ThinLTO: Can't get an output file for the "
                               "remarks");
          }

          // Parse module now
          auto TheModule = loadModuleFromInput(Mod.get(), Context, false,
                                               /*IsImporting*/ false);

          // Save temps: original file.
          saveTempBitcode(*TheModule, SaveTempsDir, count, ".0.original.bc");

          // Run the main process now, and generates a binary
          auto OutputBuffer = ProcessThinLTOModule(
              *TheModule, *Index, ModuleMap, *TMBuilder.create(),
              ImportLists[ModuleIdentifier], ExportLists[ModuleIdentifier],
              GUIDPreservedSymbols,
              ModuleToDefinedGVSummaries[ModuleIdentifier], CacheOptions,
              DisableCodeGen, SaveTempsDir, Freestanding, OptLevel, count,
              DebugPassManager);

          Infos[count].ComputedBuffer = std::move(OutputBuffer);

          unsigned CacheState = Infos[count].State.fetch_or(MS_ComputationDone);
          bool CacheHit = CacheState & MS_CacheLookupHit;

          // Consider writing the object file.
          if (DeterministicCheck) {
            // With deterministic checks, writing the object file is always our
            // responsibility.
            WriteObject(count, /*CacheHit=*/false);
          } else {
            // Without deterministic checks, writing the object file is
            // responsibility of whoever comes up with usable result first.
            if (!(CacheState & MS_CacheLookupDone) || !CacheHit)
              WriteObject(count, /*CacheHit=*/false);
          }

          // Enqueueing cache write is responsibility of whoever lost the race.
          if (DeterministicCheck) {
            if (CacheState & MS_CacheLookupDone)
              EnqueueCacheWrite(count);
          } else {
            if (CacheState & MS_CacheLookupDone) {
              if (!CacheHit)
                EnqueueCacheWrite(count);
              else
                EnqueueCacheWrite(-1);
            }
          }
        });
      }

      WrittenObjects.wait(AllModules);

      {
        if (CacheLogging)
          CacheLogOS.applyLocked([&](raw_ostream &OS) {
            OS << "Waiting for outstanding cache requests...\n";
          });
        ScopedDurationTimer T([&](double Seconds) {
          if (CacheLogging)
            CacheLogOS.applyLocked([&](raw_ostream &OS) {
              OS << "Handled outstanding cache requests in "
                 << llvm::format("%.6fs", Seconds) << "\n";
            });
        });
        auto Start = std::chrono::steady_clock::now();
        auto CacheTimeout = DeterministicCheck
                                ? std::chrono::milliseconds::max()
                                : std::chrono::milliseconds(5000);

        if (!HandledCacheReads.waitFor(CacheTimeout, AllModules)) {
          // If we were unable to finish all cache reads in time, just request
          // their cancellation (we already have all objects written) and don't
          // bother writing to the cache (that would probably be even slower
          // than reading form it).
          GetCancelTok->requestCancellation();
        } else {
          auto Now = std::chrono::steady_clock::now();
          auto RemainingCacheTimeout = CacheTimeout - (Now - Start);
          // If we finished all cache reads in time, request writes.
          if (!HandledCacheWrites.waitFor(RemainingCacheTimeout, AllModules)) {
            // If we were unable to finish all cache writes in time, request
            // their cancellation. We don't want to hold up the link any longer.
            PutCancelTok->requestCancellation();
          }
        }

        if (DeterministicCheck)
          for (int count : ModulesOrdering)
            (void)Infos[count].Entry->areLoadedAndWrittenResultsIdentical();
      }
    }

    pruneCache(CacheOptions.Path, CacheOptions.Policy, ProducedBinaries);

    // If statistics were requested, print them out now.
    if (llvm::AreStatisticsEnabled())
      llvm::PrintStatistics();
    reportAndResetTimings();

    return;
  }

  // Parallel optimizer + codegen
  {
    DefaultThreadPool Pool(heavyweight_hardware_concurrency(ThreadCount));
    for (auto IndexCount : ModulesOrdering) {
      auto &Mod = Modules[IndexCount];
      Pool.async([&](int count) {
        auto ModuleIdentifier = Mod->getName();
        ScopedDurationTimer ScopedTime([&](double Seconds) {
          if (CacheLogging) {
            CacheLogOS.applyLocked([&](raw_ostream &OS) {
              OS << "LTO processing '" << ModuleIdentifier << "' in "
                 << llvm::format("%.6fs", Seconds) << "\n";
            });
          }
        });

        auto &ExportList = ExportLists[ModuleIdentifier];

        auto &DefinedGVSummaries = ModuleToDefinedGVSummaries[ModuleIdentifier];

        // Compute the output name.
        std::string OutputPath = computeThinLTOOutputPath(
            count, SavedObjectsDirectoryPath, TMBuilder);

        // The module may be cached, this helps handling it.
        auto CacheEntry = createModuleCacheEntry(
            *Index, ModuleIdentifier, OutputPath, ImportLists[ModuleIdentifier],
            ExportList, ResolvedODR[ModuleIdentifier], DefinedGVSummaries,
            OptLevel, Freestanding, TMBuilder,
            [&CacheLogOS](llvm::function_ref<void(raw_ostream & OS)> Log) {
              if (CacheLogging)
                CacheLogOS.applyLocked(Log);
            });
        auto CacheEntryPath = CacheEntry->getEntryPath();

        {
          if (CacheLogging)
            CacheLogOS.applyLocked([&](raw_ostream &OS) {
              OS << "Look up cache entry for " << ModuleIdentifier << "\n";
            });

          auto ErrOrBuffer = CacheEntry->tryLoadingBuffer();
          LLVM_DEBUG(dbgs() << "Cache " << (ErrOrBuffer ? "hit" : "miss")
                            << " '" << CacheEntryPath << "' for buffer "
                            << count << " " << ModuleIdentifier << "\n");
          if (CacheLogging)
            CacheLogOS.applyLocked([&](raw_ostream &OS) {
              OS << "Cache " << (ErrOrBuffer ? "hit" : "miss") << " '"
                 << CacheEntryPath << "' for buffer " << count << " "
                 << ModuleIdentifier << "\n";
            });

          if (ErrOrBuffer) {
            // Cache Hit!
            if (UseBufferAPI)
              ProducedBinaries[count] = std::move(ErrOrBuffer.get());
            else
              ProducedBinaryFiles[count] = writeGeneratedObject(
                  OutputPath, CacheEntry.get(), *ErrOrBuffer.get());

            if (!DeterministicCheck)
              return;
          }
        }

        LLVMContext Context;
        Context.setDiscardValueNames(LTODiscardValueNames);
        Context.enableDebugTypeODRUniquing();
        auto DiagFileOrErr = lto::setupLLVMOptimizationRemarks(
            Context, RemarksFilename, RemarksPasses, RemarksFormat,
            RemarksWithHotness, RemarksHotnessThreshold, count);
        if (!DiagFileOrErr) {
          errs() << "Error: " << toString(DiagFileOrErr.takeError()) << "\n";
          report_fatal_error("ThinLTO: Can't get an output file for the "
                             "remarks");
        }

        // Parse module now
        auto TheModule = loadModuleFromInput(Mod.get(), Context, false,
                                             /*IsImporting*/ false);

        // Save temps: original file.
        saveTempBitcode(*TheModule, SaveTempsDir, count, ".0.original.bc");

        auto &ImportList = ImportLists[ModuleIdentifier];
        // Run the main process now, and generates a binary
        auto OutputBuffer = ProcessThinLTOModule(
            *TheModule, *Index, ModuleMap, *TMBuilder.create(), ImportList,
            ExportList, GUIDPreservedSymbols,
            ModuleToDefinedGVSummaries[ModuleIdentifier], CacheOptions,
            DisableCodeGen, SaveTempsDir, Freestanding, OptLevel, count,
            DebugPassManager);

        if (CacheLogging)
          CacheLogOS.applyLocked([&](raw_ostream &OS) {
            OS << "Update cached result for " << ModuleIdentifier << "\n";
          });

        // Commit to the cache.
        CacheEntry->write(*OutputBuffer);

        if (UseBufferAPI) {
          // We need to generated a memory buffer for the linker.
          auto ReloadedBuffer = CacheEntry->getMappedBuffer();
          // When cache is enabled, reload from the cache if possible.
          // Releasing the buffer from the heap and reloading it from the
          // cache file with mmap helps us to lower memory pressure.
          // The freed memory can be used for the next input file.
          // The final binary link will read from the VFS cache (hopefully!)
          // or from disk (if the memory pressure was too high).
          if (ReloadedBuffer)
            OutputBuffer = std::move(*ReloadedBuffer);

          ProducedBinaries[count] = std::move(OutputBuffer);
          return;
        }
        ProducedBinaryFiles[count] =
            writeGeneratedObject(OutputPath, CacheEntry.get(), *OutputBuffer);
      }, IndexCount);
    }
  }

  pruneCache(CacheOptions.Path, CacheOptions.Policy, ProducedBinaries);

  // If statistics were requested, print them out now.
  if (llvm::AreStatisticsEnabled())
    llvm::PrintStatistics();
  reportAndResetTimings();
}

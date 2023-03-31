//===- cc1depscand_main.cpp - Clang CC1 Dependency Scanning Daemon --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cc1depscanProtocol.h"
#include "clang/Basic/DiagnosticCAS.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/Stack.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/CodeGen/ObjectFilePCHContainerOperations.h"
#include "clang/Config/config.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/CompileJobCacheKey.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/FrontendTool/Utils.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningService.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningTool.h"
#include "clang/Tooling/DependencyScanning/ScanAndUpdateArgs.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/CAS/ActionCache.h"
#include "llvm/CAS/CASProvidingFileSystem.h"
#include "llvm/CAS/CachingOnDiskFileSystem.h"
#include "llvm/CAS/HierarchicalTreeBuilder.h"
#include "llvm/CAS/ObjectStore.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/BLAKE3.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrefixMapper.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/ScopedDurationTimer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/VirtualOutputBackends.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
#include <mutex>
#include <shared_mutex>

#if LLVM_ON_UNIX
#include <sys/file.h> // FIXME: Unix-only. Not portable.
#include <sys/signal.h> // FIXME: Unix-only. Not portable.

#ifdef CLANG_HAVE_RLIMITS
#include <sys/resource.h>
#endif

using namespace clang;
using namespace llvm::opt;
using cc1depscand::DepscanSharing;
using clang::tooling::dependencies::DepscanPrefixMapping;
using llvm::Error;

#define DEBUG_TYPE "cc1depscand"

ALWAYS_ENABLED_STATISTIC(NumRequests, "Number of -cc1 update requests");

#ifdef CLANG_HAVE_RLIMITS
#if defined(__linux__) && defined(__PIE__)
static size_t getCurrentStackAllocation() {
  // If we can't compute the current stack usage, allow for 512K of command
  // line arguments and environment.
  size_t Usage = 512 * 1024;
  if (FILE *StatFile = fopen("/proc/self/stat", "r")) {
    // We assume that the stack extends from its current address to the end of
    // the environment space. In reality, there is another string literal (the
    // program name) after the environment, but this is close enough (we only
    // need to be within 100K or so).
    unsigned long StackPtr, EnvEnd;
    // Disable silly GCC -Wformat warning that complains about length
    // modifiers on ignored format specifiers. We want to retain these
    // for documentation purposes even though they have no effect.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#endif
    if (fscanf(StatFile,
               "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*lu %*lu %*lu %*lu %*lu "
               "%*lu %*ld %*ld %*ld %*ld %*ld %*ld %*llu %*lu %*ld %*lu %*lu "
               "%*lu %*lu %lu %*lu %*lu %*lu %*lu %*lu %*llu %*lu %*lu %*d %*d "
               "%*u %*u %*llu %*lu %*ld %*lu %*lu %*lu %*lu %*lu %*lu %lu %*d",
               &StackPtr, &EnvEnd) == 2) {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
      Usage = StackPtr < EnvEnd ? EnvEnd - StackPtr : StackPtr - EnvEnd;
    }
    fclose(StatFile);
  }
  return Usage;
}

#include <alloca.h>

LLVM_ATTRIBUTE_NOINLINE
static void ensureStackAddressSpace() {
  // Linux kernels prior to 4.1 will sometimes locate the heap of a PIE binary
  // relatively close to the stack (they are only guaranteed to be 128MiB
  // apart). This results in crashes if we happen to heap-allocate more than
  // 128MiB before we reach our stack high-water mark.
  //
  // To avoid these crashes, ensure that we have sufficient virtual memory
  // pages allocated before we start running.
  size_t Curr = getCurrentStackAllocation();
  const int kTargetStack = DesiredStackSize - 256 * 1024;
  if (Curr < kTargetStack) {
    volatile char *volatile Alloc =
        static_cast<volatile char *>(alloca(kTargetStack - Curr));
    Alloc[0] = 0;
    Alloc[kTargetStack - Curr - 1] = 0;
  }
}
#else
static void ensureStackAddressSpace() {}
#endif

/// Attempt to ensure that we have at least 8MiB of usable stack space.
static void ensureSufficientStack() {
  struct rlimit rlim;
  if (getrlimit(RLIMIT_STACK, &rlim) != 0)
    return;

  // Increase the soft stack limit to our desired level, if necessary and
  // possible.
  if (rlim.rlim_cur != RLIM_INFINITY &&
      rlim.rlim_cur < rlim_t(DesiredStackSize)) {
    // Try to allocate sufficient stack.
    if (rlim.rlim_max == RLIM_INFINITY ||
        rlim.rlim_max >= rlim_t(DesiredStackSize))
      rlim.rlim_cur = DesiredStackSize;
    else if (rlim.rlim_cur == rlim.rlim_max)
      return;
    else
      rlim.rlim_cur = rlim.rlim_max;

    if (setrlimit(RLIMIT_STACK, &rlim) != 0 ||
        rlim.rlim_cur != DesiredStackSize)
      return;
  }


  // We should now have a stack of size at least DesiredStackSize. Ensure
  // that we can actually use that much, if necessary.
  ensureStackAddressSpace();
}
#else
static void ensureSufficientStack() {}
#endif

static void reportAsFatalIfError(llvm::Error E) {
  if (E)
    llvm::report_fatal_error(std::move(E));
}

template <typename T> static T reportAsFatalIfError(Expected<T> ValOrErr) {
  if (!ValOrErr)
    reportAsFatalIfError(ValOrErr.takeError());
  return std::move(*ValOrErr);
}

namespace {
class OneOffCompilationDatabase : public tooling::CompilationDatabase {
public:
  OneOffCompilationDatabase() = delete;
  template <class... ArgsT>
  OneOffCompilationDatabase(ArgsT &&... Args)
      : Command(std::forward<ArgsT>(Args)...) {}

  std::vector<tooling::CompileCommand>
  getCompileCommands(StringRef FilePath) const override {
    return {Command};
  }

  std::vector<tooling::CompileCommand> getAllCompileCommands() const override {
    return {Command};
  }

private:
  tooling::CompileCommand Command;
};
}

namespace {
class SharedStream {
public:
  SharedStream(raw_ostream &OS) : OS(OS) {}
  void applyLocked(llvm::function_ref<void(raw_ostream &OS)> Fn) {
    std::unique_lock<std::mutex> LockGuard(Lock);
    Fn(OS);
    OS.flush();
  }

private:
  std::mutex Lock;
  raw_ostream &OS;
};
} // namespace

namespace {
/// FIXME: Move to LLVMSupport; probably llvm/Support/Process.h.
///
/// TODO: Get this working on Linux:
/// - Reading `/proc/[pid]/comm` for the command names.
/// - Walk up the `ppid` fields in `/proc/[pid]/stat`.
struct ProcessAncestor {
  uint64_t PID = ~0ULL;
  uint64_t PPID = ~0ULL;
  StringRef Name;
};
class ProcessAncestorIterator
    : public llvm::iterator_facade_base<ProcessAncestorIterator,
                                        std::forward_iterator_tag,
                                        const ProcessAncestor> {

public:
  ProcessAncestorIterator() = default;

  static uint64_t getThisPID();
  static uint64_t getParentPID();

  static ProcessAncestorIterator getThisBegin() {
    return ProcessAncestorIterator().setPID(getThisPID());
  }
  static ProcessAncestorIterator getParentBegin() {
    return ProcessAncestorIterator().setPID(getParentPID());
  }

  const ProcessAncestor &operator*() const { return Ancestor; }
  ProcessAncestorIterator &operator++() { return setPID(Ancestor.PPID); }
  bool operator==(const ProcessAncestorIterator &RHS) const {
    return Ancestor.PID == RHS.Ancestor.PID;
  }

private:
  ProcessAncestorIterator &setPID(uint64_t NewPID);

  ProcessAncestor Ancestor;
#ifdef USE_APPLE_LIBPROC_FOR_DEPSCAN_ANCESTORS
  proc_bsdinfo ProcInfo;
#endif
};
} // end namespace

uint64_t ProcessAncestorIterator::getThisPID() {
  // FIXME: Not portable.
  return ::getpid();
}

uint64_t ProcessAncestorIterator::getParentPID() {
  // FIXME: Not portable.
  return ::getppid();
}

ProcessAncestorIterator &ProcessAncestorIterator::setPID(uint64_t NewPID) {
  // Reset state in case NewPID isn't found.
  Ancestor = ProcessAncestor();

#ifdef USE_APPLE_LIBPROC_FOR_DEPSCAN_ANCESTORS
  pid_t TypeCorrectPID = NewPID;
  if (proc_pidinfo(TypeCorrectPID, PROC_PIDTBSDINFO, 0, &ProcInfo,
                   sizeof(ProcInfo)) != sizeof(ProcInfo))
    return *this; // Not found or no access.

  Ancestor.PID = NewPID;
  Ancestor.PPID = ProcInfo.pbi_ppid;
  Ancestor.Name = StringRef(ProcInfo.pbi_name);
#else
  (void)NewPID;
#endif
  return *this;
}

static Optional<std::string>
makeDepscanDaemonKey(StringRef Mode, const DepscanSharing &Sharing) {
  auto completeKey = [&Sharing](llvm::BLAKE3 &Hasher) -> std::string {
    // Only share depscan daemons that originated from the same clang version.
    Hasher.update(getClangFullVersion());
    for (const char *Arg : Sharing.CASArgs)
      Hasher.update(StringRef(Arg));
    // Using same hash size as the module cache hash.
    auto Hash = Hasher.final<sizeof(uint64_t)>();
    uint64_t HashVal =
        llvm::support::endian::read<uint64_t, llvm::support::native>(
            Hash.data());
    return toString(llvm::APInt(64, HashVal), 36, /*Signed=*/false);
  };

  auto makePIDKey = [&completeKey](uint64_t PID) -> std::string {
    llvm::BLAKE3 Hasher;
    Hasher.update(
        llvm::makeArrayRef(reinterpret_cast<uint8_t *>(&PID), sizeof(PID)));
    return completeKey(Hasher);
  };
  auto makeIdentifierKey = [&completeKey](StringRef Ident) -> std::string {
    llvm::BLAKE3 Hasher;
    Hasher.update(Ident);
    return completeKey(Hasher);
  };

  if (Sharing.ShareViaIdentifier)
    return makeIdentifierKey(Sharing.Name.getValue());

  if (Sharing.Name) {
    // Check for fast path, which doesn't need to look up process names:
    // -fdepscan-share-parent without -fdepscan-share-stop.
    if (Sharing.Name->empty() && !Sharing.Stop)
      return makePIDKey(ProcessAncestorIterator::getParentPID());

    // Check the parent's process name, and then process ancestors.
    for (ProcessAncestorIterator I = ProcessAncestorIterator::getParentBegin(),
                                 IE;
         I != IE; ++I) {
      if (I->Name == Sharing.Stop)
        break;
      if (Sharing.Name->empty() || I->Name == *Sharing.Name)
        return makePIDKey(I->PID);
      if (Sharing.OnlyShareParent)
        break;
    }

    // Fall through if the process to share isn't found.
  }

  // Still daemonize, but use the PID from this process as the key to avoid
  // sharing state.
  if (Mode == "daemon")
    return makePIDKey(ProcessAncestorIterator::getThisPID());

  // Mode == "auto".
  //
  // TODO: consider returning ThisPID (same as "daemon") once the daemon can
  // share a CAS instance without sharing filesystem caching. Or maybe delete
  // "auto" at that point and make "-fdepscan" default to "-fdepscan=daemon".
  return None;
}

static Optional<std::string>
makeDepscanDaemonPath(StringRef Mode, const DepscanSharing &Sharing) {
  if (Mode == "inline")
    return None;

  if (Sharing.Path)
    return Sharing.Path->str();

  if (auto Key = makeDepscanDaemonKey(Mode, Sharing))
    return cc1depscand::getBasePath(*Key);

  return None;
}

static Expected<llvm::cas::CASID> scanAndUpdateCC1Inline(
    const char *Exec, ArrayRef<const char *> InputArgs,
    StringRef WorkingDirectory, SmallVectorImpl<const char *> &OutputArgs,
    bool ProduceIncludeTree, bool &DiagnosticErrorOccurred,
    const DepscanPrefixMapping &PrefixMapping,
    llvm::function_ref<const char *(const Twine &)> SaveArg,
    const CASOptions &CASOpts, std::shared_ptr<llvm::cas::ObjectStore> DB,
    std::shared_ptr<llvm::cas::ActionCache> Cache);

static Expected<llvm::cas::CASID> scanAndUpdateCC1InlineWithTool(
    tooling::dependencies::DependencyScanningTool &Tool,
    DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS, const char *Exec,
    ArrayRef<const char *> InputArgs, StringRef WorkingDirectory,
    SmallVectorImpl<const char *> &OutputArgs,
    const DepscanPrefixMapping &PrefixMapping, llvm::cas::ObjectStore &DB,
    llvm::function_ref<const char *(const Twine &)> SaveArg);

static void shutdownCC1ScanDepsDaemon(StringRef Path) {
  using namespace clang::cc1depscand;
  SmallString<128> WorkingDirectory;
  reportAsFatalIfError(
      llvm::errorCodeToError(llvm::sys::fs::current_path(WorkingDirectory)));

  // llvm::dbgs() << "connecting to daemon...\n";
  auto Daemon = ScanDaemon::connectToDaemonAndShakeHands(Path);

  if (!Daemon) {
    logAllUnhandledErrors(Daemon.takeError(), llvm::errs(),
                          "Cannot connect to the daemon to shutdown: ");
    return;
  }
  CC1DepScanDProtocol Comms(*Daemon);

  DepscanPrefixMapping Mapping;
  const char *Args[] = {"-shutdown", nullptr};
  // llvm::dbgs() << "sending shutdown request...\n";
  reportAsFatalIfError(Comms.putCommand(WorkingDirectory, Args[0], Mapping));

  // Wait for the ack before return.
  CC1DepScanDProtocol::ResultKind Result;
  reportAsFatalIfError(Comms.getResultKind(Result));

  if (Result != CC1DepScanDProtocol::SuccessResult)
    llvm::report_fatal_error("Daemon shutdown failed");
}

static llvm::Expected<llvm::cas::CASID> scanAndUpdateCC1UsingDaemon(
    const char *Exec, ArrayRef<const char *> OldArgs,
    StringRef WorkingDirectory, SmallVectorImpl<const char *> &NewArgs,
    bool &DiagnosticErrorOccurred, const DepscanPrefixMapping &Mapping,
    StringRef Path, const DepscanSharing &Sharing,
    llvm::function_ref<const char *(const Twine &)> SaveArg,
    llvm::cas::ObjectStore &CAS) {
  using namespace clang::cc1depscand;

  // FIXME: Skip some of this if -fcas-fs has been passed.

  bool NoSpawnDaemon = (bool)Sharing.Path;
  // llvm::dbgs() << "connecting to daemon...\n";
  auto Daemon = NoSpawnDaemon
                    ? ScanDaemon::connectToDaemonAndShakeHands(Path)
                    : ScanDaemon::constructAndShakeHands(Path, Exec, Sharing);
  if (!Daemon)
    return Daemon.takeError();
  CC1DepScanDProtocol Comms(*Daemon);

  // llvm::dbgs() << "sending request...\n";
  if (auto E = Comms.putCommand(WorkingDirectory, OldArgs, Mapping))
    return std::move(E);

  llvm::BumpPtrAllocator Alloc;
  llvm::StringSaver Saver(Alloc);
  SmallVector<const char *> RawNewArgs;
  StringRef DiagnosticOutput;
  CC1DepScanDProtocol::ResultKind Result;
  StringRef FailedReason;
  StringRef RootID;
  if (auto E = Comms.getScanResult(Saver, Result, FailedReason, RootID,
                                   RawNewArgs, DiagnosticOutput))
    return std::move(E);

  if (Result != CC1DepScanDProtocol::SuccessResult)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "depscan daemon failed: " + FailedReason);

  // FIXME: Avoid this duplication.
  NewArgs.resize(RawNewArgs.size());
  for (int I = 0, E = RawNewArgs.size(); I != E; ++I)
    NewArgs[I] = SaveArg(RawNewArgs[I]);

  DiagnosticErrorOccurred = !DiagnosticOutput.empty();
  if (DiagnosticErrorOccurred) {
    llvm::errs() << DiagnosticOutput;
  }

  return CAS.parseID(RootID);
}

// FIXME: This is a copy of Command::writeResponseFile. Command is too deeply
// tied with clang::Driver to use directly.
static void writeResponseFile(raw_ostream &OS,
                              SmallVectorImpl<const char *> &Arguments) {
  for (const auto *Arg : Arguments) {
    OS << '"';

    for (; *Arg != '\0'; Arg++) {
      if (*Arg == '\"' || *Arg == '\\') {
        OS << '\\';
      }
      OS << *Arg;
    }

    OS << "\" ";
  }
  OS << "\n";
}

static DepscanPrefixMapping
parseCASFSAutoPrefixMappings(DiagnosticsEngine &Diag, const ArgList &Args) {
  using namespace clang::driver;
  DepscanPrefixMapping Mapping;
  for (const Arg *A : Args.filtered(options::OPT_fdepscan_prefix_map_EQ)) {
    StringRef Map = A->getValue();
    size_t Equals = Map.find('=');
    if (Equals == StringRef::npos)
      Diag.Report(diag::err_drv_invalid_argument_to_option)
          << Map << A->getOption().getName();
    else
      Mapping.PrefixMap.push_back(Map);
    A->claim();
  }
  if (const Arg *A = Args.getLastArg(options::OPT_fdepscan_prefix_map_sdk_EQ))
    Mapping.NewSDKPath = A->getValue();
  if (const Arg *A =
          Args.getLastArg(options::OPT_fdepscan_prefix_map_toolchain_EQ))
    Mapping.NewToolchainPath = A->getValue();

  return Mapping;
}

static int scanAndUpdateCC1(const char *Exec, ArrayRef<const char *> OldArgs,
                            SmallVectorImpl<const char *> &NewArgs,
                            DiagnosticsEngine &Diag,
                            const llvm::opt::ArgList &Args,
                            const CASOptions &CASOpts,
                            std::shared_ptr<llvm::cas::ObjectStore> DB,
                            std::shared_ptr<llvm::cas::ActionCache> Cache,
                            llvm::Optional<llvm::cas::CASID> &RootID) {
  using namespace clang::driver;

  auto ElapsedDiag = [&Diag](double Seconds) {
    Diag.Report(diag::remark_compile_job_cache_timing_depscan)
        << llvm::format("%.6fs", Seconds);
  };
  llvm::ScopedDurationTimer<decltype(ElapsedDiag)> ScopedTime(
      std::move(ElapsedDiag));

  StringRef WorkingDirectory;
  SmallString<128> WorkingDirectoryBuf;
  if (auto *Arg =
          Args.getLastArg(clang::driver::options::OPT_working_directory)) {
    WorkingDirectory = Arg->getValue();
  } else {
    if (llvm::Error E = llvm::errorCodeToError(
            llvm::sys::fs::current_path(WorkingDirectoryBuf))) {
      Diag.Report(diag::err_cas_depscan_failed) << std::move(E);
      return 1;
    }
    WorkingDirectory = WorkingDirectoryBuf;
  }

  // Collect these before returning to ensure they're claimed.
  DepscanSharing Sharing;
  if (Arg *A = Args.getLastArg(options::OPT_fdepscan_share_stop_EQ))
    Sharing.Stop = A->getValue();
  if (Arg *A = Args.getLastArg(options::OPT_fdepscan_share_EQ,
                               options::OPT_fdepscan_share_identifier,
                               options::OPT_fdepscan_share_parent,
                               options::OPT_fdepscan_share_parent_EQ,
                               options::OPT_fno_depscan_share)) {
    if (A->getOption().matches(options::OPT_fdepscan_share_EQ) ||
        A->getOption().matches(options::OPT_fdepscan_share_parent_EQ)) {
      Sharing.Name = A->getValue();
      Sharing.OnlyShareParent =
          A->getOption().matches(options::OPT_fdepscan_share_parent_EQ);
    } else if (A->getOption().matches(options::OPT_fdepscan_share_parent)) {
      Sharing.Name = "";
      Sharing.OnlyShareParent = true;
    } else if (A->getOption().matches(options::OPT_fdepscan_share_identifier)) {
      Sharing.Name = A->getValue();
      Sharing.ShareViaIdentifier = true;
    }
  }
  if (Arg *A = Args.getLastArg(options::OPT_fdepscan_daemon_EQ))
    Sharing.Path = A->getValue();

  StringRef Mode = "auto";
  if (Arg *A = Args.getLastArg(clang::driver::options::OPT_fdepscan_EQ)) {
    Mode = A->getValue();
    // Note: -cc1depscan does not accept '-fdepscan=off'.
    if (Mode != "daemon" && Mode != "inline" && Mode != "auto") {
      Diag.Report(diag::err_drv_invalid_argument_to_option)
          << Mode << A->getOption().getName();
      return 1;
    }
  }

  bool ProduceIncludeTree = Args.hasArg(options::OPT_fdepscan_include_tree);

  DepscanPrefixMapping PrefixMapping = parseCASFSAutoPrefixMappings(Diag, Args);

  auto SaveArg = [&Args](const Twine &T) { return Args.MakeArgString(T); };
  CompilerInvocation::GenerateCASArgs(CASOpts, Sharing.CASArgs, SaveArg);
  if (ProduceIncludeTree)
    Sharing.CASArgs.push_back("-fdepscan-include-tree");

  bool DiagnosticErrorOccurred = false;
  auto ScanAndUpdate = [&]() {
    if (Optional<std::string> DaemonPath = makeDepscanDaemonPath(Mode, Sharing))
      return scanAndUpdateCC1UsingDaemon(
          Exec, OldArgs, WorkingDirectory, NewArgs, DiagnosticErrorOccurred,
          PrefixMapping, *DaemonPath, Sharing, SaveArg, *DB);
    return scanAndUpdateCC1Inline(Exec, OldArgs, WorkingDirectory, NewArgs,
                                  ProduceIncludeTree, DiagnosticErrorOccurred,
                                  PrefixMapping, SaveArg, CASOpts, DB, Cache);
  };
  if (llvm::Error E = ScanAndUpdate().moveInto(RootID)) {
    Diag.Report(diag::err_cas_depscan_failed) << std::move(E);
    return 1;
  }
  return DiagnosticErrorOccurred;
}

int cc1depscan_main(ArrayRef<const char *> Argv, const char *Argv0,
                    void *MainAddr) {
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts;
  {
    auto FoundCC1Args =
        std::find_if(Argv.begin(), Argv.end(), [](const char *Arg) -> bool {
          return StringRef(Arg).equals("-cc1-args");
        });
    if (FoundCC1Args != Argv.end()) {
      SmallVector<const char *, 8> WarnOpts{Argv0};
      WarnOpts.append(FoundCC1Args + 1, Argv.end());
      DiagOpts = CreateAndPopulateDiagOpts(WarnOpts);
    } else {
      DiagOpts = new DiagnosticOptions();
    }
  }
  auto DiagsConsumer = std::make_unique<TextDiagnosticPrinter>(
      llvm::errs(), DiagOpts.get(), false);
  DiagnosticsEngine Diags(new DiagnosticIDs(), DiagOpts);
  Diags.setClient(DiagsConsumer.get(), /*ShouldOwnClient=*/false);
  ProcessWarningOptions(Diags, *DiagOpts);
  if (Diags.hasErrorOccurred())
    return 1;

  // FIXME: Create a new OptionFlag group for cc1depscan.
  const OptTable &Opts = clang::driver::getDriverOptTable();
  unsigned MissingArgIndex, MissingArgCount;
  auto Args = Opts.ParseArgs(Argv, MissingArgIndex, MissingArgCount);
  if (MissingArgCount) {
    Diags.Report(diag::err_drv_missing_argument)
        << Args.getArgString(MissingArgIndex) << MissingArgCount;
    return 1;
  }

  auto *CC1Args = Args.getLastArg(clang::driver::options::OPT_cc1_args);
  if (!CC1Args) {
    llvm::errs() << "missing -cc1-args option\n";
    return 1;
  }

  auto *OutputArg = Args.getLastArg(clang::driver::options::OPT_o);
  std::string OutputPath = OutputArg ? OutputArg->getValue() : "-";

  Optional<StringRef> DumpDepscanTree;
  if (auto *Arg =
          Args.getLastArg(clang::driver::options::OPT_dump_depscan_tree_EQ))
    DumpDepscanTree = Arg->getValue();

  SmallVector<const char *> NewArgs;
  Optional<llvm::cas::CASID> RootID;

  CASOptions CASOpts;
  auto ParsedCC1Args =
      Opts.ParseArgs(CC1Args->getValues(), MissingArgIndex, MissingArgCount);
  CompilerInvocation::ParseCASArgs(CASOpts, ParsedCC1Args, Diags);
  CASOpts.ensurePersistentCAS();

  std::shared_ptr<llvm::cas::ObjectStore> CAS =
      CASOpts.getOrCreateObjectStore(Diags);
  if (!CAS)
    return 1;

  std::shared_ptr<llvm::cas::ActionCache> Cache =
      CASOpts.getOrCreateActionCache(Diags);
  if (!Cache)
    return 1;

  if (int Ret = scanAndUpdateCC1(Argv0, CC1Args->getValues(), NewArgs, Diags,
                                 Args, CASOpts, CAS, Cache, RootID))
    return Ret;

  // FIXME: Use OutputBackend to OnDisk only now.
  auto OutputBackend =
      llvm::makeIntrusiveRefCnt<llvm::vfs::OnDiskOutputBackend>();
  auto OutputFile = consumeDiscardOnDestroy(
      OutputBackend->createFile(OutputPath, llvm::vfs::OutputConfig()
                                                .setTextWithCRLF(true)
                                                .setDiscardOnSignal(false)
                                                .setAtomicWrite(false)));
  if (!OutputFile) {
    Diags.Report(diag::err_fe_unable_to_open_output)
        << OutputArg->getValue() << llvm::toString(OutputFile.takeError());
    return 1;
  }

  if (DumpDepscanTree) {
    std::error_code EC;
    llvm::raw_fd_ostream RootOS(*DumpDepscanTree, EC);
    if (EC)
      Diags.Report(diag::err_fe_unable_to_open_output)
          << *DumpDepscanTree << EC.message();
    RootOS << RootID->toString() << "\n";
  }
  writeResponseFile(*OutputFile, NewArgs);

  if (auto Err = OutputFile->keep()) {
    llvm::errs() << "failed closing outputfile: "
                 << llvm::toString(std::move(Err)) << "\n";
    return 1;
  }
  return 0;
}

int cc1depscand_main(ArrayRef<const char *> Argv, const char *Argv0,
                     void *MainAddr) {
  ensureSufficientStack();

  if (Argv.size() < 2)
    llvm::report_fatal_error(
        "clang -cc1depscand: missing command and base-path");

  StringRef Command = Argv[0];
  StringRef BasePath = Argv[1];

  // Shutdown test mode. In shutdown test mode, daemon will open acception, but
  // not replying anything, just tear down the connect immediately.
  bool ShutDownTest = false;
  bool KeepAlive = false;
  bool Detached = false;
  bool Debug = false;
  bool SingleCommandMode = false;

  // Whether the daemon can safely stay alive a longer period of time.
  // FIXME: Consider designing a mechanism to notify daemons, started for a
  // particular "build session", to shutdown, then have it stay alive until the
  // session is finished.
  bool LongRunning = false;

  // List of cas options.
  ArrayRef<const char *> CASArgs;

  for (const auto *A = Argv.begin() + 2; A != Argv.end(); ++A) {
    StringRef Arg(*A);
    if (Arg == "-shutdown")
      ShutDownTest = true;
    else if (Arg == "-detach")
      Detached = true;
    else if (Arg == "-long-running")
      LongRunning = true;
    else if (Arg == "-single-command")
      SingleCommandMode = true;
    else if (Arg == "-debug") {
      // Debug mode. Running in detach mode.
      Debug = true;
      Detached = true;
    } else if (Arg == "-cas-args") {
      CASArgs = llvm::makeArrayRef(A + 1, Argv.end());
      break;
    }
  }

  auto formSpawnArgsForCommand =
      [&](const char *Command) -> SmallVector<const char *> {
    SmallVector<const char *> Args{Argv0,     "-cc1depscand",
                                   Command,   BasePath.begin(),
                                   "-detach", "-cas-args"};
    Args.append(CASArgs.begin(), CASArgs.end());
    Args.push_back(nullptr);
    return Args;
  };

  if (Command == "-launch") {
    signal(SIGCHLD, SIG_IGN);
    auto Args = formSpawnArgsForCommand("-run");
    int IgnoredPid;
    int EC = ::posix_spawn(&IgnoredPid, Args[0], /*file_actions=*/nullptr,
                           /*attrp=*/nullptr, const_cast<char **>(Args.data()),
                           /*envp=*/nullptr);
    if (EC)
      llvm::report_fatal_error("clang -cc1depscand: failed to daemonize");
    ::exit(0);
  }

  if (Command == "-start") {
    KeepAlive = true;
    llvm::EnableStatistics(/*DoPrintOnExit=*/true);
    if (!Detached) {
      signal(SIGCHLD, SIG_IGN);
      auto Args = formSpawnArgsForCommand("-start");
      int IgnoredPid;
      int EC =
          ::posix_spawn(&IgnoredPid, Args[0], /*file_actions=*/nullptr,
                        /*attrp=*/nullptr, const_cast<char **>(Args.data()),
                        /*envp=*/nullptr);
      if (EC)
        llvm::report_fatal_error("clang -cc1depscand: failed to daemonize");
      ::exit(0);
    }
  }

  if (Command == "-shutdown") {
    // When shutdown command is received, connect to daemon and sent shuwdown
    // command.
    shutdownCC1ScanDepsDaemon(BasePath);
    ::exit(0);
  }

  if (Command != "-run" && Command != "-start")
    llvm::report_fatal_error("clang -cc1depscand: unknown command '" + Command +
                             "'");

  // Daemonize.
  if (::signal(SIGHUP, SIG_IGN) == SIG_ERR)
    llvm::report_fatal_error("clang -cc1depscand: failed to ignore SIGHUP");
  if (!Debug) {
    if (::setsid() == -1)
      llvm::report_fatal_error("clang -cc1depscand: setsid failed");
  }

  // Check the pidfile.
  SmallString<128> PidPath, LogOutPath, LogErrPath;
  (BasePath + ".pid").toVector(PidPath);
  (BasePath + ".out").toVector(LogOutPath);
  (BasePath + ".err").toVector(LogErrPath);

  // Create the base directory if necessary.
  StringRef BaseDir = llvm::sys::path::parent_path(BasePath);
  if (std::error_code EC = llvm::sys::fs::create_directories(BaseDir))
    llvm::report_fatal_error(
        Twine("clang -cc1depscand: cannot create basedir: ") + EC.message());

  auto openAndReplaceFD = [&](int ReplacedFD, StringRef Path) {
    int FD;
    if (std::error_code EC = llvm::sys::fs::openFile(
            Path, FD, llvm::sys::fs::CD_CreateAlways, llvm::sys::fs::FA_Write,
            llvm::sys::fs::OF_None)) {
      // Ignoring error?
      ::close(ReplacedFD);
      return;
    }
    ::dup2(FD, ReplacedFD);
    ::close(FD);
  };
  openAndReplaceFD(1, LogOutPath);
  openAndReplaceFD(2, LogErrPath);

  bool ShouldKeepOutputs = true;
  auto DropOutputs = llvm::make_scope_exit([&]() {
    if (ShouldKeepOutputs)
      return;
    ::unlink(LogOutPath.c_str());
    ::unlink(LogErrPath.c_str());
  });

  int PidFD;
  [&]() {
    if (std::error_code EC = llvm::sys::fs::openFile(
            PidPath, PidFD, llvm::sys::fs::CD_OpenAlways,
            llvm::sys::fs::FA_Write, llvm::sys::fs::OF_None))
      llvm::report_fatal_error("clang -cc1depscand: cannot open pidfile");

    // Try to lock; failure means there's another daemon running.
    if (::flock(PidFD, LOCK_EX | LOCK_NB))
      ::exit(0);

    // FIXME: Should we actually write the pid here? Maybe we don't care.
  }();

  // Clean up the pidfile when we're done.
  auto ClosePidFile = [&]() {
    if (PidFD != -1)
      ::close(PidFD);
    PidFD = -1;
  };
  auto ClosePidFileAtExit = llvm::make_scope_exit([&]() { ClosePidFile(); });

  // Open the socket and start listening.
  int ListenSocket = cc1depscand::createSocket();
  if (ListenSocket == -1)
    llvm::report_fatal_error("clang -cc1depscand: cannot open socket");

  if (cc1depscand::bindToSocket(BasePath, ListenSocket))
    llvm::report_fatal_error(StringRef() +
                             "clang -cc1depscand: cannot bind to socket" +
                             ": " + strerror(errno));
  bool IsBound = true;
  auto RemoveBindFile = [&] {
    assert(IsBound);
    cc1depscand::unlinkBoundSocket(BasePath);
    IsBound = false;
  };
  auto RemoveBindFileAtExit = llvm::make_scope_exit([&]() {
    if (IsBound)
      RemoveBindFile();
  });

  llvm::ThreadPool Pool;
  if (::listen(ListenSocket, /*MaxBacklog=*/Pool.getThreadCount() * 16))
    llvm::report_fatal_error("clang -cc1depscand: cannot listen to socket");

  auto ShutdownCleanUp = [&]() {
    RemoveBindFile();
    ClosePidFile();
    ::close(ListenSocket);
  };

  DiagnosticsEngine Diags(new DiagnosticIDs(), new DiagnosticOptions());
  CASOptions CASOpts;
  const OptTable &Opts = clang::driver::getDriverOptTable();
  unsigned MissingArgIndex, MissingArgCount;
  auto ParsedCASArgs =
      Opts.ParseArgs(CASArgs, MissingArgIndex, MissingArgCount);
  CompilerInvocation::ParseCASArgs(CASOpts, ParsedCASArgs, Diags);
  CASOpts.ensurePersistentCAS();
  bool ProduceIncludeTree =
      ParsedCASArgs.hasArg(driver::options::OPT_fdepscan_include_tree);

  std::shared_ptr<llvm::cas::ObjectStore> CAS =
      CASOpts.getOrCreateObjectStore(Diags);
  if (!CAS)
    llvm::report_fatal_error("clang -cc1depscand: cannot create CAS");

  std::shared_ptr<llvm::cas::ActionCache> Cache =
      CASOpts.getOrCreateActionCache(Diags);
  if (!Cache)
    llvm::report_fatal_error("clang -cc1depscand: cannot create ActionCache");

  IntrusiveRefCntPtr<llvm::cas::CachingOnDiskFileSystem> FS;
  if (!ProduceIncludeTree)
    FS = llvm::cantFail(llvm::cas::createCachingOnDiskFileSystem(*CAS));
  tooling::dependencies::DependencyScanningService Service(
      tooling::dependencies::ScanningMode::DependencyDirectivesScan,
      ProduceIncludeTree
          ? tooling::dependencies::ScanningOutputFormat::IncludeTree
          : tooling::dependencies::ScanningOutputFormat::Tree,
      CASOpts, Cache, FS,
      /*ReuseFileManager=*/false,
      /*SkipExcludedPPRanges=*/true);

  std::atomic<bool> ShutDown(false);
  std::atomic<int> NumRunning(0);

  std::chrono::steady_clock::time_point Start =
      std::chrono::steady_clock::now();
  std::atomic<uint64_t> SecondsSinceLastClose;

  SharedStream SharedOS(llvm::errs());

#ifndef NDEBUG
  if (ShutDownTest)
    llvm::outs() << "launched in shutdown test state\n";
#endif

  auto ServiceLoop = [&CAS, &Service, &ShutDown, &ListenSocket, &NumRunning,
                      &Start, &SecondsSinceLastClose, Argv0, &SharedOS,
                      ShutDownTest, &ShutdownCleanUp,
                      SingleCommandMode](unsigned I) {
    Optional<tooling::dependencies::DependencyScanningTool> Tool;
    SmallString<256> Message;
    while (true) {
      if (ShutDown.load())
        return;

      int Data = cc1depscand::acceptSocket(ListenSocket);
      if (Data == -1)
        continue;

      auto CloseData = llvm::make_scope_exit([&]() { ::close(Data); });
      cc1depscand::CC1DepScanDProtocol Comms(Data);

      auto StopRunning = llvm::make_scope_exit([&]() {
        SecondsSinceLastClose.store(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - Start)
                .count());
        --NumRunning;
      });

      {
        ++NumRunning;

        // In test mode, just tear down everything.
        if (ShutDownTest) {
          ShutdownCleanUp();
          ShutDown.store(true);
          continue;
        }
        // Check again for shutdown, since the main thread could have
        // requested it before we created the service.
        //
        // FIXME: Return Optional<ServiceReference> from the map, handling
        // this condition in getOrCreateService().
        if (ShutDown.load()) {
          // Abort the work in shutdown state since the thread can go down
          // anytime.
          return; // FIXME: Tell the client about this?
        }
      }

      // First put a result kind as a handshake.
      if (auto E = Comms.putResultKind(
              cc1depscand::CC1DepScanDProtocol::SuccessResult)) {
        SharedOS.applyLocked([&](raw_ostream &OS) {
          OS << I << ": failed to send handshake\n";
          logAllUnhandledErrors(std::move(E), OS);
        });
        continue; // go back to wait when handshake failed.
      }

      llvm::BumpPtrAllocator Alloc;
      llvm::StringSaver Saver(Alloc);
      StringRef WorkingDirectory;
      SmallVector<const char *> Args;
      DepscanPrefixMapping PrefixMapping;
      if (llvm::Error E =
              Comms.getCommand(Saver, WorkingDirectory, Args, PrefixMapping)) {
        SharedOS.applyLocked([&](raw_ostream &OS) {
          OS << I << ": failed to get command\n";
          logAllUnhandledErrors(std::move(E), OS);
        });
        continue; // FIXME: Tell the client something went wrong.
      }

      if (StringRef(Args[0]) == "-shutdown") {
        consumeError(Comms.putResultKind(
            cc1depscand::CC1DepScanDProtocol::SuccessResult));
        ShutdownCleanUp();
        ShutDown.store(true);
        continue;
      }

      // cc1 request.
      ++NumRequests;
      auto printScannedCC1 = [&](raw_ostream &OS) {
        OS << I << ": scanned -cc1:";
        for (const char *Arg : Args)
          OS << " " << Arg;
        OS << "\n";
      };

      bool ProduceIncludeTree =
          Service.getFormat() ==
          tooling::dependencies::ScanningOutputFormat::IncludeTree;

      // Is this safe to reuse? Or does DependendencyScanningWorkerFileSystem
      // make some bad assumptions about relative paths?
      if (!Tool) {
        llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> UnderlyingFS =
            llvm::vfs::createPhysicalFileSystem();
        if (ProduceIncludeTree)
          UnderlyingFS = llvm::cas::createCASProvidingFileSystem(
              CAS, std::move(UnderlyingFS));
        Tool.emplace(Service, std::move(UnderlyingFS));
      }

      IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts =
          CreateAndPopulateDiagOpts(Args);
      SmallString<128> DiagsBuffer;
      llvm::raw_svector_ostream DiagsOS(DiagsBuffer);
      DiagsOS.enable_colors(true);
      auto DiagsConsumer = std::make_unique<TextDiagnosticPrinter>(
          DiagsOS, DiagOpts.get(), false);

      SmallVector<const char *> NewArgs;
      auto RootID = scanAndUpdateCC1InlineWithTool(
          *Tool, *DiagsConsumer, &DiagsOS, Argv0, Args, WorkingDirectory,
          NewArgs, PrefixMapping, *CAS,
          [&](const Twine &T) { return Saver.save(T).data(); });
      if (!RootID) {
        consumeError(Comms.putScanResultFailed(toString(RootID.takeError())));
        SharedOS.applyLocked([&](raw_ostream &OS) {
          printScannedCC1(OS);
          OS << I << ": failed to create compiler invocation\n";
          OS << DiagsBuffer;
        });
        continue;
      }

      auto printComputedCC1 = [&](raw_ostream &OS) {
        OS << I << ": sending back new -cc1 args:\n";
        for (const char *Arg : NewArgs)
          OS << " " << Arg;
        OS << "\n";
      };
      if (llvm::Error E = Comms.putScanResultSuccess(RootID->toString(),
                                                     NewArgs, DiagsOS.str())) {
        SharedOS.applyLocked([&](raw_ostream &OS) {
          printScannedCC1(OS);
          printComputedCC1(OS);
          logAllUnhandledErrors(std::move(E), OS);
        });
        continue; // FIXME: Tell the client something went wrong.
      }

      // Done!
#ifndef NDEBUG
      // In +asserts mode, print out -cc1s even on success.
      SharedOS.applyLocked([&](raw_ostream &OS) {
        printScannedCC1(OS);
        printComputedCC1(OS);
      });
#endif
      if (SingleCommandMode) {
        ShutdownCleanUp();
        ShutDown.store(true);
        return;
      }
    }
  };

  if (SingleCommandMode) {
    // If in run once mode. Run it single thread then exit.
    ServiceLoop(0);
    ::exit(0);
  }

  for (unsigned I = 0; I < Pool.getThreadCount(); ++I)
    Pool.async(ServiceLoop, I);

  // Wait for the work to finish.
  const uint64_t SecondsBetweenAttempts = 5;
  const uint64_t SecondsBeforeDestruction = LongRunning ? 45 : 15;
  uint64_t SleepTime = SecondsBeforeDestruction;
  while (true) {
    ::sleep(SleepTime);
    SleepTime = SecondsBetweenAttempts;

    if (NumRunning.load())
      continue;

    if (ShutDown.load())
      break;

    if (KeepAlive)
      continue;

    // Figure out the latest access time that we'll delete.
    uint64_t LastAccessToDestroy =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - Start)
            .count();
    if (LastAccessToDestroy < SecondsBeforeDestruction)
      continue; // In case ::sleep returns slightly early.
    LastAccessToDestroy -= SecondsBeforeDestruction;

    if (LastAccessToDestroy < SecondsSinceLastClose)
      continue;

    // Tear down the socket and bind file immediately but wait till all existing
    // jobs to finish.
    ShutdownCleanUp();
    ShutDown.store(true);
  }

  // Exit instead of return. Otherwise, it will wait on ~ThreadPool() which will
  // never return since all threads might still be sleeping on ::accept().
  ::exit(0);
}

static Expected<llvm::cas::CASID> scanAndUpdateCC1InlineWithTool(
    tooling::dependencies::DependencyScanningTool &Tool,
    DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS, const char *Exec,
    ArrayRef<const char *> InputArgs, StringRef WorkingDirectory,
    SmallVectorImpl<const char *> &OutputArgs,
    const DepscanPrefixMapping &PrefixMapping, llvm::cas::ObjectStore &DB,
    llvm::function_ref<const char *(const Twine &)> SaveArg) {
  DiagnosticsEngine Diags(new DiagnosticIDs(), new DiagnosticOptions());
  Diags.setClient(&DiagsConsumer, /*ShouldOwnClient=*/false);
  auto Invocation = std::make_shared<CompilerInvocation>();
  if (!CompilerInvocation::CreateFromArgs(*Invocation, InputArgs, Diags, Exec))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to create compiler invocation");

  Expected<llvm::cas::CASID> Root = scanAndUpdateCC1InlineWithTool(
      Tool, DiagsConsumer, VerboseOS, *Invocation, WorkingDirectory,
      PrefixMapping, DB);
  if (!Root)
    return Root;

  OutputArgs.resize(1);
  OutputArgs[0] = "-cc1";
  Invocation->generateCC1CommandLine(OutputArgs, SaveArg);
  return *Root;
}

static Expected<llvm::cas::CASID> scanAndUpdateCC1Inline(
    const char *Exec, ArrayRef<const char *> InputArgs,
    StringRef WorkingDirectory, SmallVectorImpl<const char *> &OutputArgs,
    bool ProduceIncludeTree, bool &DiagnosticErrorOccurred,
    const DepscanPrefixMapping &PrefixMapping,
    llvm::function_ref<const char *(const Twine &)> SaveArg,
    const CASOptions &CASOpts, std::shared_ptr<llvm::cas::ObjectStore> DB,
    std::shared_ptr<llvm::cas::ActionCache> Cache) {
  IntrusiveRefCntPtr<llvm::cas::CachingOnDiskFileSystem> FS;
  if (!ProduceIncludeTree)
    FS = llvm::cantFail(llvm::cas::createCachingOnDiskFileSystem(*DB));

  tooling::dependencies::DependencyScanningService Service(
      tooling::dependencies::ScanningMode::DependencyDirectivesScan,
      ProduceIncludeTree
          ? tooling::dependencies::ScanningOutputFormat::IncludeTree
          : tooling::dependencies::ScanningOutputFormat::Tree,
      CASOpts, Cache, FS,
      /*ReuseFileManager=*/false,
      /*SkipExcludedPPRanges=*/true);
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> UnderlyingFS =
      llvm::vfs::createPhysicalFileSystem();
  if (ProduceIncludeTree)
    UnderlyingFS =
        llvm::cas::createCASProvidingFileSystem(DB, std::move(UnderlyingFS));
  tooling::dependencies::DependencyScanningTool Tool(Service,
                                                     std::move(UnderlyingFS));

  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts =
      CreateAndPopulateDiagOpts(InputArgs);
  auto DiagsConsumer = std::make_unique<TextDiagnosticPrinter>(
      llvm::errs(), DiagOpts.get(), false);

  auto Result = scanAndUpdateCC1InlineWithTool(
      Tool, *DiagsConsumer, /*VerboseOS*/ nullptr, Exec, InputArgs,
      WorkingDirectory, OutputArgs, PrefixMapping, *DB, SaveArg);
  DiagnosticErrorOccurred = DiagsConsumer->getNumErrors() != 0;
  return Result;
}
#endif /* LLVM_ON_UNIX */

//===-- CacheLauncherMode.cpp - clang-cache driver mode -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CacheLauncherMode.h"
#include "clang/Basic/DiagnosticCAS.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/StringSaver.h"

using namespace clang;

static bool isSameProgram(StringRef clangCachePath, StringRef compilerPath) {
  // Fast path check, see if they have the same parent path.
  if (llvm::sys::path::parent_path(clangCachePath) ==
      llvm::sys::path::parent_path(compilerPath))
    return true;
  // Check the file status IDs;
  llvm::sys::fs::file_status CacheStat, CompilerStat;
  if (llvm::sys::fs::status(clangCachePath, CacheStat))
    return false;
  if (llvm::sys::fs::status(compilerPath, CompilerStat))
    return false;
  return CacheStat.getUniqueID() == CompilerStat.getUniqueID();
}

static bool shouldCacheInvocation(ArrayRef<const char *> Args,
                                  IntrusiveRefCntPtr<DiagnosticsEngine> Diags) {
  SmallVector<const char *, 128> CheckArgs(Args.begin(), Args.end());
  // Make sure "-###" is not present otherwise we won't get an object back.
  CheckArgs.erase(
      llvm::remove_if(CheckArgs, [](StringRef Arg) { return Arg == "-###"; }),
      CheckArgs.end());
  // 'ShouldRecoverOnErorrs' enables picking the first invocation in a
  // multi-arch build.
  std::shared_ptr<CompilerInvocation> CInvok = createInvocationFromCommandLine(
      CheckArgs, Diags, /*VFS*/ nullptr, /*ShouldRecoverOnErorrs*/ true);
  if (!CInvok)
    return false;
  if (CInvok->getLangOpts()->Modules) {
    Diags->Report(diag::warn_clang_cache_disabled_caching)
        << "-fmodules is enabled";
    return false;
  }
  if (CInvok->getLangOpts()->AsmPreprocessor) {
    Diags->Report(diag::warn_clang_cache_disabled_caching)
        << "assembler language mode is enabled";
    return false;
  }
  if (llvm::sys::Process::GetEnv("AS_SECURE_LOG_FILE")) {
    // AS_SECURE_LOG_FILE causes uncaptured output in MC assembler.
    Diags->Report(diag::warn_clang_cache_disabled_caching)
        << "AS_SECURE_LOG_FILE is set";
    return false;
  }
  return true;
}

static int executeAsProcess(ArrayRef<const char *> Args,
                            DiagnosticsEngine &Diags) {
  SmallVector<StringRef, 128> RefArgs;
  RefArgs.reserve(Args.size());
  for (const char *Arg : Args) {
    RefArgs.push_back(Arg);
  }
  std::string ErrMsg;
  int Result = llvm::sys::ExecuteAndWait(RefArgs[0], RefArgs, /*Env*/ None,
                                         /*Redirects*/ {}, /*SecondsToWait*/ 0,
                                         /*MemoryLimit*/ 0, &ErrMsg);
  if (!ErrMsg.empty()) {
    Diags.Report(diag::err_clang_cache_failed_execution) << ErrMsg;
  }
  return Result;
}

Optional<int>
clang::handleClangCacheInvocation(SmallVectorImpl<const char *> &Args,
                                  llvm::StringSaver &Saver) {
  assert(Args.size() >= 1);

  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts;
  if (Optional<std::string> WarnOptsValue =
          llvm::sys::Process::GetEnv("LLVM_CACHE_WARNINGS")) {
    SmallVector<const char *, 8> WarnOpts;
    WarnOpts.push_back(Args.front());
    llvm::cl::TokenizeGNUCommandLine(*WarnOptsValue, Saver, WarnOpts);
    DiagOpts = CreateAndPopulateDiagOpts(WarnOpts);
  } else {
    DiagOpts = new DiagnosticOptions();
  }
  auto DiagsConsumer = std::make_unique<TextDiagnosticPrinter>(
      llvm::errs(), DiagOpts.get(), false);
  IntrusiveRefCntPtr<DiagnosticsEngine> DiagsPtr(
      new DiagnosticsEngine(new DiagnosticIDs(), DiagOpts));
  DiagnosticsEngine &Diags = *DiagsPtr;
  Diags.setClient(DiagsConsumer.get(), /*ShouldOwnClient=*/false);
  ProcessWarningOptions(Diags, *DiagOpts);
  if (Diags.hasErrorOccurred())
    return 1;

  if (Args.size() == 1) {
    // FIXME: With just 'clang-cache' invocation consider outputting info, like
    // the on-disk CAS path and its size.
    Diags.Report(diag::err_clang_cache_missing_compiler_command);
    return 1;
  }

  const char *clangCachePath = Args.front();
  // Drop initial '/path/to/clang-cache' program name.
  Args.erase(Args.begin());

  llvm::ErrorOr<std::string> compilerPathOrErr =
      llvm::sys::findProgramByName(Args.front());
  if (!compilerPathOrErr) {
    Diags.Report(diag::err_clang_cache_cannot_find_binary) << Args.front();
    return 1;
  }
  std::string compilerPath = std::move(*compilerPathOrErr);
  if (Args.front() != compilerPath)
    Args[0] = Saver.save(compilerPath).data();

  if (isSameProgram(clangCachePath, compilerPath)) {
    if (!shouldCacheInvocation(Args, DiagsPtr)) {
      if (Diags.hasErrorOccurred())
        return 1;
      return None;
    }
    if (const char *SessionId = ::getenv("LLVM_CACHE_BUILD_SESSION_ID")) {
      // `LLVM_CACHE_BUILD_SESSION_ID` enables sharing of a depscan daemon
      // using the string it is set to. The clang invocations under the same
      // `LLVM_CACHE_BUILD_SESSION_ID` will launch and re-use the same daemon.
      //
      // This is a scheme where we are still launching daemons on-demand,
      // instead of a scheme where we start a daemon at the beginning of the
      // "build session" for all clang invocations to connect to.
      // Launcing daemons on-demand is preferable because it allows having mixed
      // toolchains, with different clang versions, running under the same
      // `LLVM_CACHE_BUILD_SESSION_ID`; in such a case there will be one daemon
      // started and shared for each unique clang version.
      Args.append(
          {"-fdepscan=daemon", "-fdepscan-share-identifier", SessionId});
    } else {
      Args.push_back("-fdepscan");
    }
    if (::getenv("CLANG_CACHE_ENABLE_INCLUDE_TREE")) {
      Args.push_back("-fdepscan-include-tree");
    }
    if (const char *PrefixMaps = ::getenv("LLVM_CACHE_PREFIX_MAPS")) {
      Args.append({"-fdepscan-prefix-map-sdk=/^sdk",
                   "-fdepscan-prefix-map-toolchain=/^toolchain"});
      StringRef PrefixMap, Remaining = PrefixMaps;
      while (true) {
        std::tie(PrefixMap, Remaining) = Remaining.split(';');
        if (PrefixMap.empty())
          break;
        Args.push_back(Saver.save("-fdepscan-prefix-map=" + PrefixMap).data());
      }
    }
    if (const char *ServicePath =
            ::getenv("LLVM_CACHE_REMOTE_SERVICE_SOCKET_PATH")) {
      Args.append({"-Xclang", "-fcompilation-caching-service-path", "-Xclang",
                   ServicePath});
    }
    if (const char *CASPath = ::getenv("LLVM_CACHE_CAS_PATH")) {
      llvm::SmallString<256> CASArg(CASPath);
      llvm::sys::path::append(CASArg, "cas");
      Args.append({"-Xclang", "-fcas-path", "-Xclang",
                   Saver.save(CASArg.str()).data()});
      llvm::SmallString<256> CacheArg(CASPath);
      llvm::sys::path::append(CacheArg, "actioncache");
      Args.append({"-Xclang", "-faction-cache-path", "-Xclang",
                   Saver.save(CacheArg.str()).data()});
    }
    Args.append({"-greproducible"});

    if (llvm::sys::Process::GetEnv("CLANG_CACHE_REDACT_TIME_MACROS")) {
      // Remove use of these macros to get reproducible outputs. This can
      // accompany CLANG_CACHE_TEST_DETERMINISTIC_OUTPUTS to avoid fatal errors
      // when the source uses these macros.
      Args.append({"-Wno-builtin-macro-redefined", "-D__DATE__=\"redacted\"",
                   "-D__TIMESTAMP__=\"redacted\"", "-D__TIME__=\"redacted\""});
    }
    if (llvm::sys::Process::GetEnv(
            "CLANG_CACHE_CHECK_REPRODUCIBLE_CACHING_ISSUES")) {
      Args.append({"-Werror=reproducible-caching"});
    }
    if (llvm::sys::Process::GetEnv("CLANG_CACHE_TEST_DETERMINISTIC_OUTPUTS")) {
      // Run the compilation twice, without replaying, to check that we get the
      // same compilation artifacts for the same key. If they are not the same
      // the action cache will trigger a fatal error.
      Args.append({"-Xclang", "-fcache-disable-replay"});
      int Result = executeAsProcess(Args, Diags);
      if (Result != 0)
        return Result;
    }
    return None;
  }

  // FIXME: If it's invoking a different clang binary determine whether that
  // clang supports the caching options, don't immediately give up on caching.

  // Not invoking same clang binary, do a normal invocation without changing
  // arguments, but warn because this may be unexpected to the user.
  Diags.Report(diag::warn_clang_cache_disabled_caching)
      << "clang-cache invokes a different clang binary than itself";

  return executeAsProcess(Args, Diags);
}

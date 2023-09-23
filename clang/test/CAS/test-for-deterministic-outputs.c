// RUN: rm -rf %t && mkdir -p %t

// This compiles twice with replay disabled, ensuring that we get the same outputs for the same key.

// RUN: env LLVM_CACHE_CAS_PATH=%t/cas CLANG_CACHE_TEST_DETERMINISTIC_OUTPUTS=1 CLANG_CACHE_REDACT_TIME_MACROS=1 %clang-cache \
// RUN:   %clang -target x86_64-apple-macos11 -c %s -o %t/t.o -Rcompile-job-cache 2> %t/out.txt
// RUN: FileCheck %s --check-prefix=CACHE-SKIPPED --input-file=%t/out.txt

// CACHE-SKIPPED: remark: compile job cache skipped
// CACHE-SKIPPED: remark: compile job cache skipped

// RUN: env LLVM_CACHE_CAS_PATH=%t/cas %clang-cache \
// RUN:   %clang -target x86_64-apple-macos11 -c %s -o %t/t.o -Rcompile-job-cache -Wreproducible-caching 2> %t/out.txt
// RUN: FileCheck %s --check-prefix=CACHE-WARN --input-file=%t/out.txt

/// Check still a cache miss.
// RUN: env LLVM_CACHE_CAS_PATH=%t/cas %clang-cache \
// RUN:   %clang -target x86_64-apple-macos11 -c %s -o %t/t.o -Rcompile-job-cache -Wreproducible-caching 2> %t/out.txt
// RUN: FileCheck %s --check-prefix=CACHE-WARN --input-file=%t/out.txt

// CACHE-WARN: remark: compile job cache miss
// CACHE-WARN: warning: encountered non-reproducible token, caching will be skipped
// CACHE-WARN: warning: encountered non-reproducible token, caching will be skipped
// CACHE-WARN: warning: encountered non-reproducible token, caching will be skipped
// CACHE-WARN: remark: compile job cache skipped

/// Check -Werror doesn't actually error when we use the launcher.
// RUN: env LLVM_CACHE_CAS_PATH=%t/cas %clang-cache \
// RUN:   %clang -target x86_64-apple-macos11 -c %s -o %t/t.o -Werror -Rcompile-job-cache 2> %t/out.txt
// RUN: FileCheck %s --check-prefix=NOERROR --input-file=%t/out.txt
// RUN: not env LLVM_CACHE_CAS_PATH=%t/cas CLANG_CACHE_CHECK_REPRODUCIBLE_CACHING_ISSUES=1 %clang-cache \
// RUN:   %clang -target x86_64-apple-macos11 -c %s -o %t/t.o -Rcompile-job-cache 2> %t/out.txt
// RUN: FileCheck %s --check-prefix=ERROR --input-file=%t/out.txt

// NOERROR-NOT: error:
// ERROR: error: encountered non-reproducible token, caching will be skipped

void getit(const char **p1, const char **p2, const char **p3) {
  *p1 = __DATE__;
  *p2 = __TIMESTAMP__;
  *p3 = __TIME__;
}

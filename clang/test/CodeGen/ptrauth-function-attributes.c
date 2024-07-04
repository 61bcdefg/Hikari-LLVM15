// RUN: %clang_cc1 -triple arm64e-apple-ios                   -emit-llvm %s  -o - | FileCheck %s --check-prefixes=ALL,OFF
// RUN: %clang_cc1 -triple aarch64-linux-gnu                  -emit-llvm %s  -o - | FileCheck %s --check-prefixes=ALL,OFF

// RUN: %clang_cc1 -triple arm64-apple-ios  -fptrauth-calls   -emit-llvm %s  -o - | FileCheck %s --check-prefixes=ALL,CALLS
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fptrauth-calls  -emit-llvm %s  -o - | FileCheck %s --check-prefixes=ALL,CALLS

// RUN: %clang_cc1 -triple arm64-apple-ios  -fptrauth-returns -emit-llvm %s  -o - | FileCheck %s --check-prefixes=ALL,RETS
// RUN: %clang_cc1 -triple arm64e-apple-ios -fptrauth-returns -emit-llvm %s  -o - | FileCheck %s --check-prefixes=ALL,RETS
// RUN: %clang_cc1 -triple arm64e-apple-ios                   -emit-llvm %s  -o - | FileCheck %s --check-prefixes=ALL,OFF

// RUN: %clang_cc1 -triple arm64-apple-ios  -fptrauth-indirect-gotos -emit-llvm %s -o - | FileCheck %s --check-prefixes=ALL,GOTOS
// RUN: %clang_cc1 -triple arm64e-apple-ios -fptrauth-indirect-gotos -emit-llvm %s -o - | FileCheck %s --check-prefixes=ALL,GOTOS
// RUN: %clang_cc1 -triple arm64e-apple-ios                          -emit-llvm %s -o - | FileCheck %s --check-prefixes=ALL,OFF

// RUN: %clang_cc1 -triple arm64-apple-ios  -fptrauth-auth-traps -emit-llvm %s -o - | FileCheck %s --check-prefixes=ALL,TRAPS
// RUN: %clang_cc1 -triple arm64e-apple-ios -fptrauth-auth-traps -emit-llvm %s -o - | FileCheck %s --check-prefixes=ALL,TRAPS
// RUN: %clang_cc1 -triple arm64e-apple-ios                      -emit-llvm %s -o - | FileCheck %s --check-prefixes=ALL,OFF

// RUN: %clang_cc1 -triple arm64e-apple-ios  -mbranch-target-enforce -emit-llvm %s -o - | FileCheck %s --check-prefixes=ALL,BTI

// ALL: define {{(dso_local )?}}void @test() #0
void test() {
}

// CALLS: attributes #0 = {{{.*}} "ptrauth-calls" {{.*}}}

// RETS: attributes #0 = {{{.*}} "ptrauth-returns" {{.*}}}
// GOTOS: attributes #0 = {{{.*}} "ptrauth-indirect-gotos" {{.*}}}
// TRAPS: attributes #0 = {{{.*}} "ptrauth-auth-traps" {{.*}}}

// BTI: !1 = !{i32 8, !"branch-target-enforcement", i32 1}

// OFF-NOT: attributes {{.*}} "ptrauth-
// OFF-NOT: !"branch-target-enforcement"

// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -emit-llvm %s  -o - | FileCheck -check-prefix=CHECK -check-prefix=NOPCH %s
// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -emit-pch %s -o %t.ast
// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -emit-llvm -x ast -o - %t.ast | FileCheck -check-prefix=CHECK -check-prefix=PCH %s

#define FNPTRKEY 0

void (*fnptr)(void);
long discriminator;

extern void external_function(void);
// CHECK: [[EXTERNAL_FUNCTION:@.*]] = private constant { ptr, i32, i64, i64 } { ptr @external_function, i32 0, i64 0, i64 0 }, section "llvm.ptrauth", align 8
// CHECK: @fptr1 = global ptr [[EXTERNAL_FUNCTION]]
void (*fptr1)(void) = external_function;
// CHECK: @fptr2 = global ptr [[EXTERNAL_FUNCTION]]
void (*fptr2)(void) = &external_function;

// CHECK: [[SIGNED:@.*]] = private constant { ptr, i32, i64, i64 } { ptr @external_function, i32 2, i64 0, i64 26 }, section "llvm.ptrauth", align 8
// CHECK: @fptr3 = global ptr [[SIGNED]]
void (*fptr3)(void) = __builtin_ptrauth_sign_constant(&external_function, 2, 26);

// CHECK: @fptr4 = global ptr [[SIGNED:@.*]],
// CHECK: [[SIGNED]] = private constant { ptr, i32, i64, i64 } { ptr @external_function, i32 2, i64 ptrtoint (ptr @fptr4 to i64), i64 26 }, section "llvm.ptrauth", align 8
void (*fptr4)(void) = __builtin_ptrauth_sign_constant(&external_function, 2, __builtin_ptrauth_blend_discriminator(&fptr4, 26));

// CHECK-LABEL: define void @test_call()
void test_call() {
  // CHECK:      [[T0:%.*]] = load ptr, ptr @fnptr,
  // CHECK-NEXT: call void [[T0]]() [ "ptrauth"(i32 0, i64 0) ]
  fnptr();
}

// CHECK-LABEL: define void @test_direct_call()
void test_direct_call() {
  // CHECK: call void @test_call(){{$}}
  test_call();
}

void abort();
// CHECK-LABEL: define void @test_direct_builtin_call()
void test_direct_builtin_call() {
  // CHECK: call void @abort() {{#[0-9]+$}}
  abort();
}

// CHECK-LABEL: define ptr @test_function_pointer()
// CHECK:        [[EXTERNAL_FUNCTION]]
void (*test_function_pointer())(void) {
  return external_function;
}

// rdar://34562484 - Handle IR types changing in the caching mechanism.
struct InitiallyIncomplete;
extern struct InitiallyIncomplete returns_initially_incomplete(void);
// CHECK-LABEL: define void @use_while_incomplete()
void use_while_incomplete() {
  // NOPCH:      [[VAR:%.*]] = alloca ptr,
  // NOPCH-NEXT: store ptr @returns_initially_incomplete.ptrauth, ptr [[VAR]],
  // PCH:        [[VAR:%.*]] = alloca ptr,
  // PCH-NEXT:   store ptr @returns_initially_incomplete.ptrauth, ptr [[VAR]],
  struct InitiallyIncomplete (*fnptr)(void) = &returns_initially_incomplete;
}
struct InitiallyIncomplete { int x; };
// CHECK-LABEL: define void @use_while_complete()
void use_while_complete() {
  // CHECK:      [[VAR:%.*]] = alloca ptr,
  // CHECK-NEXT: store ptr @returns_initially_incomplete.ptrauth, ptr [[VAR]],
  // CHECK-NEXT: ret void
  struct InitiallyIncomplete (*fnptr)(void) = &returns_initially_incomplete;
}

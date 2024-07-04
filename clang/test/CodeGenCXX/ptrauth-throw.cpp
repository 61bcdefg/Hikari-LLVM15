// RUN: %clang_cc1 -mllvm -ptrauth-emit-wrapper-globals=0 -fptrauth-function-pointer-type-discrimination -triple arm64-apple-ios -fptrauth-calls -fcxx-exceptions -emit-llvm %s -o - | FileCheck %s

class Foo {
 public:
  ~Foo() {
  }
};

// CHECK-LABEL: define void @_Z1fv()
// CHECK:  call void @__cxa_throw(ptr %{{.*}}, ptr @_ZTI3Foo, ptr ptrauth (ptr @_ZN3FooD1Ev, i32 0, i64 [[DISC:10942]]))
void f() {
  throw Foo();
}

// __cxa_throw is defined to take its destructor as "void (*)(void *)" in the ABI.
// CHECK-LABEL: define void @__cxa_throw({{.*}})
// CHECK:  call void {{%.*}}(ptr noundef {{%.*}}) [ "ptrauth"(i32 0, i64 [[DISC]]) ]
extern "C" void __cxa_throw(void *exception, void *, void (*dtor)(void *)) {
  dtor(exception);
}

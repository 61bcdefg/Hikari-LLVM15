; RUN:  llvm-dis < %s.bc | FileCheck %s --check-prefixes=CHECK,CHECK-TYPED
; callbr.ll.bc was generated by passing this file to llvm-as.

; RUN: llvm-as < %s | llvm-dis | FileCheck %s --check-prefixes=CHECK,CHECK-TYPED
; RUN: llvm-as -opaque-pointers < %s | llvm-dis -opaque-pointers | FileCheck %s --check-prefixes=CHECK,CHECK-OPAQUE

define i32 @test_asm_goto(i32 %x){
entry:
; CHECK: callbr void asm "", "r,!i"(i32 %x)
; CHECK-NEXT: to label %normal [label %fail]
  callbr void asm "", "r,!i"(i32 %x) to label %normal [label %fail]
normal:
  ret i32 1
fail:
  ret i32 0
}

define i32 @test_asm_goto2(i32 %x){
entry:
; CHECK: callbr void asm "", "r,!i,!i"(i32 %x)
; CHECK-NEXT: to label %normal [label %fail, label %fail2]
  callbr void asm "", "r,!i,!i"(i32 %x) to label %normal [label %fail, label %fail2]
normal:
  ret i32 1
fail:
  ret i32 0
fail2:
  ret i32 2
}

define i32 @test_asm_goto3(i32 %x){
entry:
; CHECK-TYPED:      callbr void asm "", "r,i,!i"(i32 %x, i8* blockaddress(@test_asm_goto3, %unrelated))
; CHECK-OPAQUE:     callbr void asm "", "r,i,!i"(i32 %x, ptr blockaddress(@test_asm_goto3, %unrelated))
; CHECK-NEXT: to label %normal [label %fail]
  callbr void asm "", "r,i,!i"(i32 %x, i8* blockaddress(@test_asm_goto3, %unrelated)) to label %normal [label %fail]
normal:
  ret i32 1
fail:
  ret i32 0
unrelated:
  ret i32 2
}
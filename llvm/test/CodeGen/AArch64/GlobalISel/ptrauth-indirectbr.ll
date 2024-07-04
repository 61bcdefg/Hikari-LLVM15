; RUN: llc -mtriple arm64e-apple-darwin -global-isel -verify-machineinstrs -global-isel-abort=1 -asm-verbose=false -aarch64-enable-collect-loh=false -o - %s | FileCheck %s

; CHECK-LABEL: _test_blockaddress:
; CHECK:         adrp x16, [[BB1ADDR:Ltmp[0-9]+]]@PAGE
; CHECK-NEXT:    add x16, x16, [[BB1ADDR]]@PAGEOFF
; CHECK-NEXT:    mov x17, #[[DISCVAL:52152]]
; CHECK-NEXT:    pacia x16, x17
; CHECK-NEXT:    mov x0, x16
; CHECK-NEXT:    adrp x16, [[BB2ADDR:Ltmp[0-9]+]]@PAGE
; CHECK-NEXT:    add x16, x16, [[BB2ADDR]]@PAGEOFF
; CHECK-NEXT:    mov x17, #[[DISCVAL]]
; CHECK-NEXT:    pacia x16, x17
; CHECK-NEXT:    mov x1, x16
; CHECK-NEXT:    bl _dummy_choose
; CHECK-NEXT:    mov x17, #[[DISCVAL]]
; CHECK-NEXT:    braa x0, x17
; CHECK:        [[BB1ADDR]]:
; CHECK-NEXT:   [[BB1:LBB[0-9_]+]]:
; CHECK-NEXT:    mov w0, #1
; CHECK:        [[BB2ADDR]]:
; CHECK-NEXT:   [[BB2:LBB[0-9_]+]]:
; CHECK-NEXT:    mov w0, #2
define i32 @test_blockaddress() #0 {
entry:
  %tmp0 = call i8* @dummy_choose(i8* blockaddress(@test_blockaddress, %bb1), i8* blockaddress(@test_blockaddress, %bb2))
  indirectbr i8* %tmp0, [label %bb1, label %bb2]

bb1:
  ret i32 1

bb2:
  ret i32 2
}

; CHECK-LABEL: _test_blockaddress_other_function:
; CHECK:         adrp x16, [[BB1ADDR]]@PAGE
; CHECK-NEXT:    add x16, x16, [[BB1ADDR]]@PAGEOFF
; CHECK-NEXT:    mov x17, #[[DISCVAL]]
; CHECK-NEXT:    pacia x16, x17
; CHECK-NEXT:    mov x0, x16
; CHECK-NEXT:    ret
define i8* @test_blockaddress_other_function() #0 {
  ret i8* blockaddress(@test_blockaddress, %bb1)
}

; CHECK-LABEL: .section __DATA,__const
; CHECK-NEXT:  .globl _test_blockaddress_array
; CHECK-NEXT:  .p2align 3
; CHECK-NEXT:  _test_blockaddress_array:
; CHECK-NEXT:   .quad [[BB1ADDR]]@AUTH(ia,[[DISCVAL]]
; CHECK-NEXT:   .quad [[BB2ADDR]]@AUTH(ia,[[DISCVAL]]
@test_blockaddress_array = constant [2 x i8*] [i8* blockaddress(@test_blockaddress, %bb1), i8* blockaddress(@test_blockaddress, %bb2)]

declare i8* @dummy_choose(i8*, i8*)

attributes #0 = { nounwind "ptrauth-returns" "ptrauth-indirect-gotos" }

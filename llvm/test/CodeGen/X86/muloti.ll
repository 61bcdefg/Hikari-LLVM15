; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=x86_64-apple-darwin | FileCheck %s
%0 = type { i64, i64 }
%1 = type { i128, i1 }

; This used to call muloti4, but that won't link with libgcc.
define %0 @x(i64 %a.coerce0, i64 %a.coerce1, i64 %b.coerce0, i64 %b.coerce1) nounwind uwtable ssp {
; CHECK-LABEL: x:
; CHECK:       ## %bb.0: ## %entry
; CHECK-NEXT:    pushq %r15
; CHECK-NEXT:    .cfi_def_cfa_offset 16
; CHECK-NEXT:    pushq %r14
; CHECK-NEXT:    .cfi_def_cfa_offset 24
; CHECK-NEXT:    pushq %rbx
; CHECK-NEXT:    .cfi_def_cfa_offset 32
; CHECK-NEXT:    .cfi_offset %rbx, -32
; CHECK-NEXT:    .cfi_offset %r14, -24
; CHECK-NEXT:    .cfi_offset %r15, -16
; CHECK-NEXT:    movq %rdx, %r11
; CHECK-NEXT:    movq %rsi, %r9
; CHECK-NEXT:    movq %rdi, %r15
; CHECK-NEXT:    sarq $63, %rsi
; CHECK-NEXT:    movq %rdx, %rdi
; CHECK-NEXT:    imulq %rsi, %rdi
; CHECK-NEXT:    movq %rdx, %rax
; CHECK-NEXT:    mulq %rsi
; CHECK-NEXT:    movq %rax, %r8
; CHECK-NEXT:    addq %rdi, %rdx
; CHECK-NEXT:    imulq %rcx, %rsi
; CHECK-NEXT:    addq %rdx, %rsi
; CHECK-NEXT:    movq %rcx, %rdi
; CHECK-NEXT:    sarq $63, %rdi
; CHECK-NEXT:    movq %rdi, %rbx
; CHECK-NEXT:    imulq %r9, %rbx
; CHECK-NEXT:    movq %rdi, %rax
; CHECK-NEXT:    mulq %r15
; CHECK-NEXT:    movq %rax, %r10
; CHECK-NEXT:    addq %rbx, %rdx
; CHECK-NEXT:    imulq %r15, %rdi
; CHECK-NEXT:    addq %rdx, %rdi
; CHECK-NEXT:    addq %r8, %r10
; CHECK-NEXT:    adcq %rsi, %rdi
; CHECK-NEXT:    movq %r15, %rax
; CHECK-NEXT:    mulq %r11
; CHECK-NEXT:    movq %rdx, %r14
; CHECK-NEXT:    movq %rax, %r8
; CHECK-NEXT:    movq %r9, %rax
; CHECK-NEXT:    mulq %r11
; CHECK-NEXT:    movq %rdx, %rbx
; CHECK-NEXT:    movq %rax, %rsi
; CHECK-NEXT:    addq %r14, %rsi
; CHECK-NEXT:    adcq $0, %rbx
; CHECK-NEXT:    movq %r15, %rax
; CHECK-NEXT:    mulq %rcx
; CHECK-NEXT:    movq %rdx, %r14
; CHECK-NEXT:    movq %rax, %r11
; CHECK-NEXT:    addq %rsi, %r11
; CHECK-NEXT:    adcq %rbx, %r14
; CHECK-NEXT:    setb %al
; CHECK-NEXT:    movzbl %al, %esi
; CHECK-NEXT:    movq %r9, %rax
; CHECK-NEXT:    mulq %rcx
; CHECK-NEXT:    addq %r14, %rax
; CHECK-NEXT:    adcq %rsi, %rdx
; CHECK-NEXT:    addq %r10, %rax
; CHECK-NEXT:    adcq %rdi, %rdx
; CHECK-NEXT:    movq %r11, %rcx
; CHECK-NEXT:    sarq $63, %rcx
; CHECK-NEXT:    xorq %rcx, %rdx
; CHECK-NEXT:    xorq %rax, %rcx
; CHECK-NEXT:    orq %rdx, %rcx
; CHECK-NEXT:    jne LBB0_1
; CHECK-NEXT:  ## %bb.2: ## %nooverflow
; CHECK-NEXT:    movq %r8, %rax
; CHECK-NEXT:    movq %r11, %rdx
; CHECK-NEXT:    popq %rbx
; CHECK-NEXT:    popq %r14
; CHECK-NEXT:    popq %r15
; CHECK-NEXT:    retq
; CHECK-NEXT:  LBB0_1: ## %overflow
; CHECK-NEXT:    ud2
entry:
  %tmp16 = zext i64 %a.coerce0 to i128
  %tmp11 = zext i64 %a.coerce1 to i128
  %tmp12 = shl nuw i128 %tmp11, 64
  %ins14 = or i128 %tmp12, %tmp16
  %tmp6 = zext i64 %b.coerce0 to i128
  %tmp3 = zext i64 %b.coerce1 to i128
  %tmp4 = shl nuw i128 %tmp3, 64
  %ins = or i128 %tmp4, %tmp6
  %0 = tail call %1 @llvm.smul.with.overflow.i128(i128 %ins14, i128 %ins)
  %1 = extractvalue %1 %0, 0
  %2 = extractvalue %1 %0, 1
  br i1 %2, label %overflow, label %nooverflow

overflow:                                         ; preds = %entry
  tail call void @llvm.trap()
  unreachable

nooverflow:                                       ; preds = %entry
  %tmp20 = trunc i128 %1 to i64
  %tmp21 = insertvalue %0 undef, i64 %tmp20, 0
  %tmp22 = lshr i128 %1, 64
  %tmp23 = trunc i128 %tmp22 to i64
  %tmp24 = insertvalue %0 %tmp21, i64 %tmp23, 1
  ret %0 %tmp24
}

declare %1 @llvm.smul.with.overflow.i128(i128, i128) nounwind readnone

declare void @llvm.trap() nounwind
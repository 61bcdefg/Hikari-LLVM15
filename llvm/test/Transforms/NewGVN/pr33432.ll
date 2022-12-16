; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt -passes=newgvn -S %s | FileCheck %s
; Ensure we do not incorrect do phi of ops
@d = external local_unnamed_addr global i32, align 4

define void @patatino() {
; CHECK-LABEL: @patatino(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[TMP0:%.*]] = load i32, i32* @d, align 4
; CHECK-NEXT:    br label [[FOR_END10:%.*]]
; CHECK:       for.end10:
; CHECK-NEXT:    [[OR:%.*]] = or i32 [[TMP0]], 8
; CHECK-NEXT:    br i1 undef, label [[IF_END:%.*]], label [[FOR_END10]]
; CHECK:       if.end:
; CHECK-NEXT:    ret void
;
entry:
  %0 = load i32, i32* @d, align 4
  br label %for.end10

for.end10:
  %f.0 = phi i32 [ undef, %entry ], [ 8, %for.end10 ]
  %or = or i32 %0, %f.0
  %mul12 = mul nsw i32 %or, undef
  br i1 undef, label %if.end, label %for.end10

if.end:
  ret void
}

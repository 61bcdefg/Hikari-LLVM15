; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt -instcombine -early-cse -earlycse-debug-hash -S < %s | FileCheck %s

define i8* @simplifyNullLaunder() {
; CHECK-LABEL: @simplifyNullLaunder(
; CHECK-NEXT:    ret i8* null
;
  %b2 = call i8* @llvm.launder.invariant.group.p0i8(i8* null)
  ret i8* %b2
}

define i8* @dontSimplifyNullLaunderNoNullOpt() #0 {
; CHECK-LABEL: @dontSimplifyNullLaunderNoNullOpt(
; CHECK-NEXT:    [[B2:%.*]] = call i8* @llvm.launder.invariant.group.p0i8(i8* null)
; CHECK-NEXT:    ret i8* [[B2]]
;
  %b2 = call i8* @llvm.launder.invariant.group.p0i8(i8* null)
  ret i8* %b2
}

define i8 addrspace(42)* @dontsimplifyNullLaunderForDifferentAddrspace() {
; CHECK-LABEL: @dontsimplifyNullLaunderForDifferentAddrspace(
; CHECK-NEXT:    [[B2:%.*]] = call i8 addrspace(42)* @llvm.launder.invariant.group.p42i8(i8 addrspace(42)* null)
; CHECK-NEXT:    ret i8 addrspace(42)* [[B2]]
;
  %b2 = call i8 addrspace(42)* @llvm.launder.invariant.group.p42i8(i8 addrspace(42)* null)
  ret i8 addrspace(42)* %b2
}

define i8* @simplifyUndefLaunder() {
; CHECK-LABEL: @simplifyUndefLaunder(
; CHECK-NEXT:    ret i8* undef
;
  %b2 = call i8* @llvm.launder.invariant.group.p0i8(i8* undef)
  ret i8* %b2
}

define i8 addrspace(42)* @simplifyUndefLaunder2() {
; CHECK-LABEL: @simplifyUndefLaunder2(
; CHECK-NEXT:    ret i8 addrspace(42)* undef
;
  %b2 = call i8 addrspace(42)* @llvm.launder.invariant.group.p42i8(i8 addrspace(42)* undef)
  ret i8 addrspace(42)* %b2
}

define i8* @simplifyNullStrip() {
; CHECK-LABEL: @simplifyNullStrip(
; CHECK-NEXT:    ret i8* null
;
  %b2 = call i8* @llvm.strip.invariant.group.p0i8(i8* null)
  ret i8* %b2
}

define i8* @dontSimplifyNullStripNonNullOpt() #0 {
; CHECK-LABEL: @dontSimplifyNullStripNonNullOpt(
; CHECK-NEXT:    [[B2:%.*]] = call i8* @llvm.strip.invariant.group.p0i8(i8* null)
; CHECK-NEXT:    ret i8* [[B2]]
;
  %b2 = call i8* @llvm.strip.invariant.group.p0i8(i8* null)
  ret i8* %b2
}

define i8 addrspace(42)* @dontsimplifyNullStripForDifferentAddrspace() {
; CHECK-LABEL: @dontsimplifyNullStripForDifferentAddrspace(
; CHECK-NEXT:    [[B2:%.*]] = call i8 addrspace(42)* @llvm.strip.invariant.group.p42i8(i8 addrspace(42)* null)
; CHECK-NEXT:    ret i8 addrspace(42)* [[B2]]
;
  %b2 = call i8 addrspace(42)* @llvm.strip.invariant.group.p42i8(i8 addrspace(42)* null)
  ret i8 addrspace(42)* %b2
}

define i8* @simplifyUndefStrip() {
; CHECK-LABEL: @simplifyUndefStrip(
; CHECK-NEXT:    ret i8* undef
;
  %b2 = call i8* @llvm.strip.invariant.group.p0i8(i8* undef)
  ret i8* %b2
}

define i8 addrspace(42)* @simplifyUndefStrip2() {
; CHECK-LABEL: @simplifyUndefStrip2(
; CHECK-NEXT:    ret i8 addrspace(42)* undef
;
  %b2 = call i8 addrspace(42)* @llvm.strip.invariant.group.p42i8(i8 addrspace(42)* undef)
  ret i8 addrspace(42)* %b2
}

define i8* @simplifyLaunderOfLaunder(i8* %a) {
; CHECK-LABEL: @simplifyLaunderOfLaunder(
; CHECK-NEXT:    [[TMP1:%.*]] = call i8* @llvm.launder.invariant.group.p0i8(i8* [[A:%.*]])
; CHECK-NEXT:    ret i8* [[TMP1]]
;
  %a2 = call i8* @llvm.launder.invariant.group.p0i8(i8* %a)
  %a3 = call i8* @llvm.launder.invariant.group.p0i8(i8* %a2)
  ret i8* %a3
}

define i8* @simplifyStripOfLaunder(i8* %a) {
; CHECK-LABEL: @simplifyStripOfLaunder(
; CHECK-NEXT:    [[TMP1:%.*]] = call i8* @llvm.strip.invariant.group.p0i8(i8* [[A:%.*]])
; CHECK-NEXT:    ret i8* [[TMP1]]
;
  %a2 = call i8* @llvm.launder.invariant.group.p0i8(i8* %a)
  %a3 = call i8* @llvm.strip.invariant.group.p0i8(i8* %a2)
  ret i8* %a3
}

define i1 @simplifyForCompare(i8* %a) {
; CHECK-LABEL: @simplifyForCompare(
; CHECK-NEXT:    [[TMP1:%.*]] = call i8* @llvm.strip.invariant.group.p0i8(i8* [[A:%.*]])
; CHECK-NEXT:    ret i1 true
;
  %a2 = call i8* @llvm.launder.invariant.group.p0i8(i8* %a)

  %a3 = call i8* @llvm.strip.invariant.group.p0i8(i8* %a2)
  %b2 = call i8* @llvm.strip.invariant.group.p0i8(i8* %a)
  %c = icmp eq i8* %a3, %b2
  ret i1 %c
}

define i16* @skipWithDifferentTypes(i8* %a) {
; CHECK-LABEL: @skipWithDifferentTypes(
; CHECK-NEXT:    [[TMP1:%.*]] = call i8* @llvm.strip.invariant.group.p0i8(i8* [[A:%.*]])
; CHECK-NEXT:    [[TMP2:%.*]] = bitcast i8* [[TMP1]] to i16*
; CHECK-NEXT:    ret i16* [[TMP2]]
;
  %a2 = call i8* @llvm.launder.invariant.group.p0i8(i8* %a)
  %c1 = bitcast i8* %a2 to i16*

  %a3 = call i16* @llvm.strip.invariant.group.p0i16(i16* %c1)
  ret i16* %a3
}

define i16 addrspace(42)* @skipWithDifferentTypesAddrspace(i8 addrspace(42)* %a) {
; CHECK-LABEL: @skipWithDifferentTypesAddrspace(
; CHECK-NEXT:    [[TMP1:%.*]] = call i8 addrspace(42)* @llvm.strip.invariant.group.p42i8(i8 addrspace(42)* [[A:%.*]])
; CHECK-NEXT:    [[TMP2:%.*]] = bitcast i8 addrspace(42)* [[TMP1]] to i16 addrspace(42)*
; CHECK-NEXT:    ret i16 addrspace(42)* [[TMP2]]
;
  %a2 = call i8 addrspace(42)* @llvm.launder.invariant.group.p42i8(i8 addrspace(42)* %a)
  %c1 = bitcast i8 addrspace(42)* %a2 to i16 addrspace(42)*

  %a3 = call i16 addrspace(42)* @llvm.strip.invariant.group.p42i16(i16 addrspace(42)* %c1)
  ret i16 addrspace(42)* %a3
}

define i16 addrspace(42)* @skipWithDifferentTypesDifferentAddrspace(i8* %a) {
; CHECK-LABEL: @skipWithDifferentTypesDifferentAddrspace(
; CHECK-NEXT:    [[TMP1:%.*]] = call i8* @llvm.strip.invariant.group.p0i8(i8* [[A:%.*]])
; CHECK-NEXT:    [[TMP2:%.*]] = bitcast i8* [[TMP1]] to i16*
; CHECK-NEXT:    [[TMP3:%.*]] = addrspacecast i16* [[TMP2]] to i16 addrspace(42)*
; CHECK-NEXT:    ret i16 addrspace(42)* [[TMP3]]
;
  %cast = addrspacecast i8* %a to i8 addrspace(42)*
  %a2 = call i8 addrspace(42)* @llvm.launder.invariant.group.p42i8(i8 addrspace(42)* %cast)
  %c1 = bitcast i8 addrspace(42)* %a2 to i16 addrspace(42)*

  %a3 = call i16 addrspace(42)* @llvm.strip.invariant.group.p42i16(i16 addrspace(42)* %c1)
  ret i16 addrspace(42)* %a3
}

define i1 @icmp_null_launder(i8* %a) {
; CHECK-LABEL: @icmp_null_launder(
; CHECK-NEXT:    [[R:%.*]] = icmp eq i8* [[A:%.*]], null
; CHECK-NEXT:    ret i1 [[R]]
;
  %a2 = call i8* @llvm.launder.invariant.group.p0i8(i8* %a)
  %r = icmp eq i8* %a2, null
  ret i1 %r
}

define i1 @icmp_null_strip(i8* %a) {
; CHECK-LABEL: @icmp_null_strip(
; CHECK-NEXT:    [[R:%.*]] = icmp eq i8* [[A:%.*]], null
; CHECK-NEXT:    ret i1 [[R]]
;
  %a2 = call i8* @llvm.strip.invariant.group.p0i8(i8* %a)
  %r = icmp eq i8* %a2, null
  ret i1 %r
}

define i1 @icmp_null_launder_valid_null(i8* %a) #0 {
; CHECK-LABEL: @icmp_null_launder_valid_null(
; CHECK-NEXT:    [[A2:%.*]] = call i8* @llvm.launder.invariant.group.p0i8(i8* [[A:%.*]])
; CHECK-NEXT:    [[R:%.*]] = icmp eq i8* [[A2]], null
; CHECK-NEXT:    ret i1 [[R]]
;
  %a2 = call i8* @llvm.launder.invariant.group.p0i8(i8* %a)
  %r = icmp eq i8* %a2, null
  ret i1 %r
}

define i1 @icmp_null_strip_valid_null(i8* %a) #0 {
; CHECK-LABEL: @icmp_null_strip_valid_null(
; CHECK-NEXT:    [[A2:%.*]] = call i8* @llvm.strip.invariant.group.p0i8(i8* [[A:%.*]])
; CHECK-NEXT:    [[R:%.*]] = icmp eq i8* [[A2]], null
; CHECK-NEXT:    ret i1 [[R]]
;
  %a2 = call i8* @llvm.strip.invariant.group.p0i8(i8* %a)
  %r = icmp eq i8* %a2, null
  ret i1 %r
}

; Check that null always becomes the RHS
define i1 @icmp_null_launder_lhs(i8* %a) {
; CHECK-LABEL: @icmp_null_launder_lhs(
; CHECK-NEXT:    [[R:%.*]] = icmp eq i8* [[A:%.*]], null
; CHECK-NEXT:    ret i1 [[R]]
;
  %a2 = call i8* @llvm.launder.invariant.group.p0i8(i8* %a)
  %r = icmp eq i8* null, %a2
  ret i1 %r
}

define i1 @icmp_null_launder_bitcasts(i32* %a) {
; CHECK-LABEL: @icmp_null_launder_bitcasts(
; CHECK-NEXT:    [[R:%.*]] = icmp eq i32* [[A:%.*]], null
; CHECK-NEXT:    ret i1 [[R]]
;
  %a2 = bitcast i32* %a to i8*
  %a3 = call i8* @llvm.launder.invariant.group.p0i8(i8* %a2)
  %a4 = bitcast i8* %a3 to i32*
  %r = icmp eq i32* %a4, null
  ret i1 %r
}

declare i8* @llvm.launder.invariant.group.p0i8(i8*)
declare i8 addrspace(42)* @llvm.launder.invariant.group.p42i8(i8 addrspace(42)*)
declare i8* @llvm.strip.invariant.group.p0i8(i8*)
declare i8 addrspace(42)* @llvm.strip.invariant.group.p42i8(i8 addrspace(42)*)
declare i16* @llvm.strip.invariant.group.p0i16(i16* %c1)
declare i16 addrspace(42)* @llvm.strip.invariant.group.p42i16(i16 addrspace(42)* %c1)

attributes #0 = { null_pointer_is_valid }
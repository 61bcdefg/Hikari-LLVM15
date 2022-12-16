; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt -basic-aa -loop-idiom < %s -S | FileCheck %s
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"
target triple = "x86_64-unknown-linux-gnu"

;; memcpy.atomic formation (atomic load & store)
define void @test1(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test1(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    [[DEST:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    call void @llvm.memcpy.element.unordered.atomic.p0i8.p0i8.i64(i8* align 1 [[DEST]], i8* align 1 [[BASE]], i64 [[SIZE:%.*]], i32 1)
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i8, i8* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i8, i8* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i8, i8* [[I_0_014]] unordered, align 1
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i8, i32 10000
  %Dest = alloca i8, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i8, i8* %Base, i64 %indvar
  %DestI = getelementptr i8, i8* %Dest, i64 %indvar
  %V = load atomic i8, i8* %I.0.014 unordered, align 1
  store atomic i8 %V, i8* %DestI unordered, align 1
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation (atomic store, normal load)
define void @test2(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test2(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    [[DEST:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    call void @llvm.memcpy.element.unordered.atomic.p0i8.p0i8.i64(i8* align 1 [[DEST]], i8* align 1 [[BASE]], i64 [[SIZE:%.*]], i32 1)
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i8, i8* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i8, i8* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load i8, i8* [[I_0_014]], align 1
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i8, i32 10000
  %Dest = alloca i8, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i8, i8* %Base, i64 %indvar
  %DestI = getelementptr i8, i8* %Dest, i64 %indvar
  %V = load i8, i8* %I.0.014, align 1
  store atomic i8 %V, i8* %DestI unordered, align 1
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation (atomic store, normal load w/ no align)
define void @test2b(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test2b(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    [[DEST:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    call void @llvm.memcpy.element.unordered.atomic.p0i8.p0i8.i64(i8* align 1 [[DEST]], i8* align 1 [[BASE]], i64 [[SIZE:%.*]], i32 1)
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i8, i8* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i8, i8* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load i8, i8* [[I_0_014]], align 1
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i8, i32 10000
  %Dest = alloca i8, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i8, i8* %Base, i64 %indvar
  %DestI = getelementptr i8, i8* %Dest, i64 %indvar
  %V = load i8, i8* %I.0.014
  store atomic i8 %V, i8* %DestI unordered, align 1
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation rejection (atomic store, normal load w/ bad align)
define void @test2c(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test2c(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i32, i32 10000, align 4
; CHECK-NEXT:    [[DEST:%.*]] = alloca i32, i32 10000, align 4
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i32, i32* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i32, i32* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load i32, i32* [[I_0_014]], align 2
; CHECK-NEXT:    store atomic i32 [[V]], i32* [[DESTI]] unordered, align 4
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE:%.*]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i32, i32 10000
  %Dest = alloca i32, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i32, i32* %Base, i64 %indvar
  %DestI = getelementptr i32, i32* %Dest, i64 %indvar
  %V = load i32, i32* %I.0.014, align 2
  store atomic i32 %V, i32* %DestI unordered, align 4
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation rejection (atomic store w/ bad align, normal load)
define void @test2d(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test2d(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i32, i32 10000, align 4
; CHECK-NEXT:    [[DEST:%.*]] = alloca i32, i32 10000, align 4
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i32, i32* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i32, i32* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load i32, i32* [[I_0_014]], align 4
; CHECK-NEXT:    store atomic i32 [[V]], i32* [[DESTI]] unordered, align 2
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE:%.*]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i32, i32 10000
  %Dest = alloca i32, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i32, i32* %Base, i64 %indvar
  %DestI = getelementptr i32, i32* %Dest, i64 %indvar
  %V = load i32, i32* %I.0.014, align 4
  store atomic i32 %V, i32* %DestI unordered, align 2
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}


;; memcpy.atomic formation (normal store, atomic load)
define void @test3(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test3(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    [[DEST:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    call void @llvm.memcpy.element.unordered.atomic.p0i8.p0i8.i64(i8* align 1 [[DEST]], i8* align 1 [[BASE]], i64 [[SIZE:%.*]], i32 1)
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i8, i8* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i8, i8* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i8, i8* [[I_0_014]] unordered, align 1
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i8, i32 10000
  %Dest = alloca i8, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i8, i8* %Base, i64 %indvar
  %DestI = getelementptr i8, i8* %Dest, i64 %indvar
  %V = load atomic i8, i8* %I.0.014 unordered, align 1
  store i8 %V, i8* %DestI, align 1
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation rejection (normal store w/ no align, atomic load)
define void @test3b(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test3b(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    [[DEST:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    call void @llvm.memcpy.element.unordered.atomic.p0i8.p0i8.i64(i8* align 1 [[DEST]], i8* align 1 [[BASE]], i64 [[SIZE:%.*]], i32 1)
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i8, i8* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i8, i8* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i8, i8* [[I_0_014]] unordered, align 1
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i8, i32 10000
  %Dest = alloca i8, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i8, i8* %Base, i64 %indvar
  %DestI = getelementptr i8, i8* %Dest, i64 %indvar
  %V = load atomic i8, i8* %I.0.014 unordered, align 1
  store i8 %V, i8* %DestI
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation rejection (normal store, atomic load w/ bad align)
define void @test3c(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test3c(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i32, i32 10000, align 4
; CHECK-NEXT:    [[DEST:%.*]] = alloca i32, i32 10000, align 4
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i32, i32* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i32, i32* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i32, i32* [[I_0_014]] unordered, align 2
; CHECK-NEXT:    store i32 [[V]], i32* [[DESTI]], align 4
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE:%.*]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i32, i32 10000
  %Dest = alloca i32, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i32, i32* %Base, i64 %indvar
  %DestI = getelementptr i32, i32* %Dest, i64 %indvar
  %V = load atomic i32, i32* %I.0.014 unordered, align 2
  store i32 %V, i32* %DestI, align 4
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation rejection (normal store w/ bad align, atomic load)
define void @test3d(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test3d(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i32, i32 10000, align 4
; CHECK-NEXT:    [[DEST:%.*]] = alloca i32, i32 10000, align 4
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i32, i32* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i32, i32* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i32, i32* [[I_0_014]] unordered, align 4
; CHECK-NEXT:    store i32 [[V]], i32* [[DESTI]], align 2
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE:%.*]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i32, i32 10000
  %Dest = alloca i32, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i32, i32* %Base, i64 %indvar
  %DestI = getelementptr i32, i32* %Dest, i64 %indvar
  %V = load atomic i32, i32* %I.0.014 unordered, align 4
  store i32 %V, i32* %DestI, align 2
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}


;; memcpy.atomic formation rejection (atomic load, ordered-atomic store)
define void @test4(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test4(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    [[DEST:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i8, i8* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i8, i8* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i8, i8* [[I_0_014]] unordered, align 1
; CHECK-NEXT:    store atomic i8 [[V]], i8* [[DESTI]] monotonic, align 1
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE:%.*]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i8, i32 10000
  %Dest = alloca i8, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i8, i8* %Base, i64 %indvar
  %DestI = getelementptr i8, i8* %Dest, i64 %indvar
  %V = load atomic i8, i8* %I.0.014 unordered, align 1
  store atomic i8 %V, i8* %DestI monotonic, align 1
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation rejection (ordered-atomic load, unordered-atomic store)
define void @test5(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test5(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    [[DEST:%.*]] = alloca i8, i32 10000, align 1
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i8, i8* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i8, i8* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i8, i8* [[I_0_014]] monotonic, align 1
; CHECK-NEXT:    store atomic i8 [[V]], i8* [[DESTI]] unordered, align 1
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE:%.*]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i8, i32 10000
  %Dest = alloca i8, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i8, i8* %Base, i64 %indvar
  %DestI = getelementptr i8, i8* %Dest, i64 %indvar
  %V = load atomic i8, i8* %I.0.014 monotonic, align 1
  store atomic i8 %V, i8* %DestI unordered, align 1
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation (atomic load & store) -- element size 2
define void @test6(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test6(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i16, i32 10000, align 2
; CHECK-NEXT:    [[BASE2:%.*]] = bitcast i16* [[BASE]] to i8*
; CHECK-NEXT:    [[DEST:%.*]] = alloca i16, i32 10000, align 2
; CHECK-NEXT:    [[DEST1:%.*]] = bitcast i16* [[DEST]] to i8*
; CHECK-NEXT:    [[TMP0:%.*]] = shl nuw i64 [[SIZE:%.*]], 1
; CHECK-NEXT:    call void @llvm.memcpy.element.unordered.atomic.p0i8.p0i8.i64(i8* align 2 [[DEST1]], i8* align 2 [[BASE2]], i64 [[TMP0]], i32 2)
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i16, i16* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i16, i16* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i16, i16* [[I_0_014]] unordered, align 2
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i16, i32 10000
  %Dest = alloca i16, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i16, i16* %Base, i64 %indvar
  %DestI = getelementptr i16, i16* %Dest, i64 %indvar
  %V = load atomic i16, i16* %I.0.014 unordered, align 2
  store atomic i16 %V, i16* %DestI unordered, align 2
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation (atomic load & store) -- element size 4
define void @test7(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test7(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i32, i32 10000, align 4
; CHECK-NEXT:    [[BASE2:%.*]] = bitcast i32* [[BASE]] to i8*
; CHECK-NEXT:    [[DEST:%.*]] = alloca i32, i32 10000, align 4
; CHECK-NEXT:    [[DEST1:%.*]] = bitcast i32* [[DEST]] to i8*
; CHECK-NEXT:    [[TMP0:%.*]] = shl nuw i64 [[SIZE:%.*]], 2
; CHECK-NEXT:    call void @llvm.memcpy.element.unordered.atomic.p0i8.p0i8.i64(i8* align 4 [[DEST1]], i8* align 4 [[BASE2]], i64 [[TMP0]], i32 4)
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i32, i32* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i32, i32* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i32, i32* [[I_0_014]] unordered, align 4
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i32, i32 10000
  %Dest = alloca i32, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i32, i32* %Base, i64 %indvar
  %DestI = getelementptr i32, i32* %Dest, i64 %indvar
  %V = load atomic i32, i32* %I.0.014 unordered, align 4
  store atomic i32 %V, i32* %DestI unordered, align 4
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation (atomic load & store) -- element size 8
define void @test8(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test8(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i64, i32 10000, align 8
; CHECK-NEXT:    [[BASE2:%.*]] = bitcast i64* [[BASE]] to i8*
; CHECK-NEXT:    [[DEST:%.*]] = alloca i64, i32 10000, align 8
; CHECK-NEXT:    [[DEST1:%.*]] = bitcast i64* [[DEST]] to i8*
; CHECK-NEXT:    [[TMP0:%.*]] = shl nuw i64 [[SIZE:%.*]], 3
; CHECK-NEXT:    call void @llvm.memcpy.element.unordered.atomic.p0i8.p0i8.i64(i8* align 8 [[DEST1]], i8* align 8 [[BASE2]], i64 [[TMP0]], i32 8)
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i64, i64* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i64, i64* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i64, i64* [[I_0_014]] unordered, align 8
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i64, i32 10000
  %Dest = alloca i64, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i64, i64* %Base, i64 %indvar
  %DestI = getelementptr i64, i64* %Dest, i64 %indvar
  %V = load atomic i64, i64* %I.0.014 unordered, align 8
  store atomic i64 %V, i64* %DestI unordered, align 8
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation rejection (atomic load & store) -- element size 16
define void @test9(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test9(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i128, i32 10000, align 8
; CHECK-NEXT:    [[BASE2:%.*]] = bitcast i128* [[BASE]] to i8*
; CHECK-NEXT:    [[DEST:%.*]] = alloca i128, i32 10000, align 8
; CHECK-NEXT:    [[DEST1:%.*]] = bitcast i128* [[DEST]] to i8*
; CHECK-NEXT:    [[TMP0:%.*]] = shl nuw i64 [[SIZE:%.*]], 4
; CHECK-NEXT:    call void @llvm.memcpy.element.unordered.atomic.p0i8.p0i8.i64(i8* align 16 [[DEST1]], i8* align 16 [[BASE2]], i64 [[TMP0]], i32 16)
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i128, i128* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i128, i128* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i128, i128* [[I_0_014]] unordered, align 16
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i128, i32 10000
  %Dest = alloca i128, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i128, i128* %Base, i64 %indvar
  %DestI = getelementptr i128, i128* %Dest, i64 %indvar
  %V = load atomic i128, i128* %I.0.014 unordered, align 16
  store atomic i128 %V, i128* %DestI unordered, align 16
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

;; memcpy.atomic formation rejection (atomic load & store) -- element size 32
define void @test10(i64 %Size) nounwind ssp {
; CHECK-LABEL: @test10(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[BASE:%.*]] = alloca i256, i32 10000, align 8
; CHECK-NEXT:    [[DEST:%.*]] = alloca i256, i32 10000, align 8
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i256, i256* [[BASE]], i64 [[INDVAR]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i256, i256* [[DEST]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load atomic i256, i256* [[I_0_014]] unordered, align 32
; CHECK-NEXT:    store atomic i256 [[V]], i256* [[DESTI]] unordered, align 32
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE:%.*]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %Base = alloca i256, i32 10000
  %Dest = alloca i256, i32 10000
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i256, i256* %Base, i64 %indvar
  %DestI = getelementptr i256, i256* %Dest, i64 %indvar
  %V = load atomic i256, i256* %I.0.014 unordered, align 32
  store atomic i256 %V, i256* %DestI unordered, align 32
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}



; Make sure that atomic memset doesn't get recognized by mistake
define void @test_nomemset(i8* %Base, i64 %Size) nounwind ssp {
; CHECK-LABEL: @test_nomemset(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[I_0_014:%.*]] = getelementptr i8, i8* [[BASE:%.*]], i64 [[INDVAR]]
; CHECK-NEXT:    store atomic i8 0, i8* [[I_0_014]] unordered, align 1
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE:%.*]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:                                           ; preds = %entry
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %I.0.014 = getelementptr i8, i8* %Base, i64 %indvar
  store atomic i8 0, i8* %I.0.014 unordered, align 1
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

; Verify that unordered memset_pattern isn't recognized.
; This is a replica of test11_pattern from basic.ll
define void @test_nomemset_pattern(i32* nocapture %P) nounwind ssp {
; CHECK-LABEL: @test_nomemset_pattern(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[ENTRY:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[ARRAYIDX:%.*]] = getelementptr i32, i32* [[P:%.*]], i64 [[INDVAR]]
; CHECK-NEXT:    store atomic i32 1, i32* [[ARRAYIDX]] unordered, align 4
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], 10000
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
entry:
  br label %for.body

for.body:                                         ; preds = %entry, %for.body
  %indvar = phi i64 [ 0, %entry ], [ %indvar.next, %for.body ]
  %arrayidx = getelementptr i32, i32* %P, i64 %indvar
  store atomic i32 1, i32* %arrayidx unordered, align 4
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, 10000
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret void
}

; Make sure that atomic memcpy or memmove don't get recognized by mistake
; when looping with positive stride
define void @test_no_memcpy_memmove1(i8* %Src, i64 %Size) {
; CHECK-LABEL: @test_no_memcpy_memmove1(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ 0, [[BB_NPH:%.*]] ], [ [[INDVAR_NEXT:%.*]], [[FOR_BODY]] ]
; CHECK-NEXT:    [[STEP:%.*]] = add nuw nsw i64 [[INDVAR]], 1
; CHECK-NEXT:    [[SRCI:%.*]] = getelementptr i8, i8* [[SRC:%.*]], i64 [[STEP]]
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr i8, i8* [[SRC]], i64 [[INDVAR]]
; CHECK-NEXT:    [[V:%.*]] = load i8, i8* [[SRCI]], align 1
; CHECK-NEXT:    store atomic i8 [[V]], i8* [[DESTI]] unordered, align 1
; CHECK-NEXT:    [[INDVAR_NEXT]] = add i64 [[INDVAR]], 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp eq i64 [[INDVAR_NEXT]], [[SIZE:%.*]]
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_END:%.*]], label [[FOR_BODY]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  br label %for.body

for.body:                                         ; preds = %bb.nph, %for.body
  %indvar = phi i64 [ 0, %bb.nph ], [ %indvar.next, %for.body ]
  %Step = add nuw nsw i64 %indvar, 1
  %SrcI = getelementptr i8, i8* %Src, i64 %Step
  %DestI = getelementptr i8, i8* %Src, i64 %indvar
  %V = load i8, i8* %SrcI, align 1
  store atomic i8 %V, i8* %DestI unordered, align 1
  %indvar.next = add i64 %indvar, 1
  %exitcond = icmp eq i64 %indvar.next, %Size
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body, %entry
  ret void
}

; Make sure that atomic memcpy or memmove don't get recognized by mistake
; when looping with negative stride
define void @test_no_memcpy_memmove2(i8* %Src, i64 %Size) {
; CHECK-LABEL: @test_no_memcpy_memmove2(
; CHECK-NEXT:  bb.nph:
; CHECK-NEXT:    [[CMP1:%.*]] = icmp sgt i64 [[SIZE:%.*]], 0
; CHECK-NEXT:    br i1 [[CMP1]], label [[FOR_BODY_PREHEADER:%.*]], label [[FOR_END:%.*]]
; CHECK:       for.body.preheader:
; CHECK-NEXT:    br label [[FOR_BODY:%.*]]
; CHECK:       for.body:
; CHECK-NEXT:    [[INDVAR:%.*]] = phi i64 [ [[STEP:%.*]], [[FOR_BODY]] ], [ [[SIZE]], [[FOR_BODY_PREHEADER]] ]
; CHECK-NEXT:    [[STEP]] = add nsw i64 [[INDVAR]], -1
; CHECK-NEXT:    [[SRCI:%.*]] = getelementptr inbounds i8, i8* [[SRC:%.*]], i64 [[STEP]]
; CHECK-NEXT:    [[V:%.*]] = load i8, i8* [[SRCI]], align 1
; CHECK-NEXT:    [[DESTI:%.*]] = getelementptr inbounds i8, i8* [[SRC]], i64 [[INDVAR]]
; CHECK-NEXT:    store atomic i8 [[V]], i8* [[DESTI]] unordered, align 1
; CHECK-NEXT:    [[EXITCOND:%.*]] = icmp sgt i64 [[INDVAR]], 1
; CHECK-NEXT:    br i1 [[EXITCOND]], label [[FOR_BODY]], label [[FOR_END_LOOPEXIT:%.*]]
; CHECK:       for.end.loopexit:
; CHECK-NEXT:    br label [[FOR_END]]
; CHECK:       for.end:
; CHECK-NEXT:    ret void
;
bb.nph:
  %cmp1 = icmp sgt i64 %Size, 0
  br i1 %cmp1, label %for.body, label %for.end

for.body:                                           ; preds = %bb.nph, %.for.body
  %indvar = phi i64 [ %Step, %for.body ], [ %Size, %bb.nph ]
  %Step = add nsw i64 %indvar, -1
  %SrcI = getelementptr inbounds i8, i8* %Src, i64 %Step
  %V = load i8, i8* %SrcI, align 1
  %DestI = getelementptr inbounds i8, i8* %Src, i64 %indvar
  store atomic i8 %V, i8* %DestI unordered, align 1
  %exitcond = icmp sgt i64 %indvar, 1
  br i1 %exitcond, label %for.body, label %for.end

for.end:                                          ; preds = %for.body, %entry
  ret void
}
; RUN: llc -stop-after=livedebugvalues -verify-machineinstrs -march=x86-64 -o - %s | FileCheck %s
;
; CHECK: DBG_VALUE $r14, 0, {{.*}}, !DIExpression(DW_OP_LLVM_entry_value, 1, DW_OP_plus_uconst, 16, DW_OP_plus_uconst, 8, DW_OP_deref)
; CHECK: DBG_VALUE $r14, 0, {{.*}}, !DIExpression(DW_OP_LLVM_entry_value, 1, DW_OP_plus_uconst, 16, DW_OP_plus_uconst, 16)

; ModuleID = '_Concurrency.bc'
source_filename = "_Concurrency.bc"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.9.0"

%swift.opaque = type opaque
%swift.type = type { i64 }
%TScG8IteratorV = type <{ %TScG, %TSb }>
%TScG = type <{ i8* }>
%TSb = type <{ i1 }>
%swift.context = type { %swift.context*, void (%swift.context*)* }

; Function Attrs: argmemonly nocallback nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #0

; Function Attrs: nocallback nofree nosync nounwind readnone speculatable willreturn
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

; Function Attrs: argmemonly nounwind
declare extern_weak swiftcc void @swift_task_dealloc(i8*) local_unnamed_addr #2

; Function Attrs: nounwind
define hidden swifttailcc void @"$sScG8IteratorV4nextxSgyYaFTY2_"(i8* swiftasync %0) #3 !dbg !885 {
  call void @llvm.dbg.declare(metadata i8* %0, metadata !902, metadata !DIExpression(DW_OP_plus_uconst, 16, DW_OP_plus_uconst, 8, DW_OP_deref)), !dbg !903
  call void @llvm.dbg.declare(metadata i8* %0, metadata !902, metadata !DIExpression(DW_OP_plus_uconst, 16, DW_OP_plus_uconst, 8, DW_OP_deref)), !dbg !903
  call void @llvm.dbg.declare(metadata i8* %0, metadata !902, metadata !DIExpression(DW_OP_plus_uconst, 16, DW_OP_plus_uconst, 8, DW_OP_deref)), !dbg !903
  call void @llvm.dbg.declare(metadata i8* %0, metadata !899, metadata !DIExpression(DW_OP_plus_uconst, 16, DW_OP_plus_uconst, 16)), !dbg !904
  %2 = getelementptr inbounds i8, i8* %0, i64 16, !dbg !905
  %3 = getelementptr inbounds i8, i8* %0, i64 56, !dbg !909
  %4 = bitcast i8* %3 to %swift.opaque**, !dbg !909
  %5 = load %swift.opaque*, %swift.opaque** %4, align 8, !dbg !909
  %6 = getelementptr inbounds i8, i8* %0, i64 32, !dbg !909
  %7 = bitcast i8* %6 to %swift.type**, !dbg !909
  %8 = load %swift.type*, %swift.type** %7, align 8, !dbg !909
  %9 = getelementptr inbounds %swift.type, %swift.type* %8, i64 -1, !dbg !909
  %10 = bitcast %swift.type* %9 to i8***, !dbg !909
  %11 = load i8**, i8*** %10, align 8, !dbg !909, !invariant.load !18, !dereferenceable !910
  %12 = getelementptr inbounds i8*, i8** %11, i64 6, !dbg !909
  %13 = bitcast i8** %12 to i32 (%swift.opaque*, i32, %swift.type*)**, !dbg !909
  %14 = load i32 (%swift.opaque*, i32, %swift.type*)*, i32 (%swift.opaque*, i32, %swift.type*)** %13, align 8, !dbg !909, !invariant.load !18
  %15 = tail call i32 %14(%swift.opaque* noalias %5, i32 1, %swift.type* %8) #4, !dbg !909
  %16 = icmp eq i32 %15, 1
  br i1 %16, label %27, label %17

17:                                               ; preds = %1
  %18 = bitcast i8* %2 to %swift.opaque**, !dbg !911
  %19 = load %swift.opaque*, %swift.opaque** %18, align 8, !dbg !911
  %20 = getelementptr inbounds i8*, i8** %11, i64 4, !dbg !913
  %21 = bitcast i8** %20 to %swift.opaque* (%swift.opaque*, %swift.opaque*, %swift.type*)**, !dbg !913
  %22 = load %swift.opaque* (%swift.opaque*, %swift.opaque*, %swift.type*)*, %swift.opaque* (%swift.opaque*, %swift.opaque*, %swift.type*)** %21, align 8, !dbg !913, !invariant.load !18
  %23 = tail call %swift.opaque* %22(%swift.opaque* noalias %19, %swift.opaque* noalias %5, %swift.type* nonnull %8) #5, !dbg !913
  %24 = getelementptr inbounds i8*, i8** %11, i64 7, !dbg !911
  %25 = bitcast i8** %24 to void (%swift.opaque*, i32, i32, %swift.type*)**, !dbg !911
  %26 = load void (%swift.opaque*, i32, i32, %swift.type*)*, void (%swift.opaque*, i32, i32, %swift.type*)** %25, align 8, !dbg !911, !invariant.load !18
  tail call void %26(%swift.opaque* noalias %19, i32 0, i32 1, %swift.type* nonnull %8) #5, !dbg !911
  br label %47, !dbg !914

27:                                               ; preds = %1
  %28 = getelementptr inbounds i8, i8* %0, i64 48, !dbg !915
  %29 = bitcast i8* %28 to i8***, !dbg !915
  %30 = load i8**, i8*** %29, align 8, !dbg !915
  %31 = getelementptr inbounds i8, i8* %0, i64 40, !dbg !915
  %32 = bitcast i8* %31 to %swift.type**, !dbg !915
  %33 = load %swift.type*, %swift.type** %32, align 8, !dbg !915
  %34 = getelementptr inbounds i8, i8* %0, i64 24, !dbg !915
  %35 = bitcast i8* %34 to %TScG8IteratorV**, !dbg !915
  %36 = load %TScG8IteratorV*, %TScG8IteratorV** %35, align 8, !dbg !915
  %37 = bitcast i8* %2 to %swift.opaque**, !dbg !915
  %38 = load %swift.opaque*, %swift.opaque** %37, align 8, !dbg !915
  %39 = getelementptr inbounds %TScG8IteratorV, %TScG8IteratorV* %36, i64 0, i32 1, i32 0, !dbg !915
  %40 = getelementptr inbounds i8*, i8** %30, i64 1, !dbg !916
  %41 = bitcast i8** %40 to void (%swift.opaque*, %swift.type*)**, !dbg !916
  %42 = load void (%swift.opaque*, %swift.type*)*, void (%swift.opaque*, %swift.type*)** %41, align 8, !dbg !916, !invariant.load !18
  tail call void %42(%swift.opaque* noalias %5, %swift.type* %33) #5, !dbg !916
  %43 = bitcast i1* %39 to i8*, !dbg !918
  store i8 1, i8* %43, align 8, !dbg !918
  %44 = getelementptr inbounds i8*, i8** %11, i64 7, !dbg !920
  %45 = bitcast i8** %44 to void (%swift.opaque*, i32, i32, %swift.type*)**, !dbg !920
  %46 = load void (%swift.opaque*, i32, i32, %swift.type*)*, void (%swift.opaque*, i32, i32, %swift.type*)** %45, align 8, !dbg !920, !invariant.load !18
  tail call void %46(%swift.opaque* noalias %38, i32 1, i32 1, %swift.type* nonnull %8) #5, !dbg !920
  br label %47, !dbg !921

47:                                               ; preds = %27, %17
  %48 = bitcast i8* %3 to i8**, !dbg !909
  %49 = load i8*, i8** %48, align 8, !dbg !922
  tail call void @llvm.lifetime.end.p0i8(i64 -1, i8* %49), !dbg !922
  tail call swiftcc void @swift_task_dealloc(i8* %49) #5, !dbg !922
  %50 = getelementptr inbounds i8, i8* %0, i64 8, !dbg !922
  %51 = bitcast i8* %50 to void (%swift.context*)**, !dbg !922
  %52 = load void (%swift.context*)*, void (%swift.context*)** %51, align 8, !dbg !922
  %53 = bitcast i8* %0 to %swift.context*, !dbg !922
  musttail call swifttailcc void %52(%swift.context* swiftasync %53) #5, !dbg !923
  ret void, !dbg !923
}

attributes #0 = { argmemonly nocallback nofree nosync nounwind willreturn }
attributes #1 = { nocallback nofree nosync nounwind readnone speculatable willreturn }
attributes #2 = { argmemonly nounwind }
attributes #3 = { nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="core2" "target-features"="+cx16,+cx8,+fxsr,+mmx,+sahf,+sse,+sse2,+sse3,+ssse3,+x87" "tune-cpu"="generic" }
attributes #4 = { nounwind readonly }
attributes #5 = { nounwind }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12, !13, !14, !15}
!llvm.dbg.cu = !{!16}
!swift.module.flags = !{!125}
!llvm.linker.options = !{!883, !884}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 12, i32 3]}
!1 = !{i32 1, !"Objective-C Version", i32 2}
!2 = !{i32 1, !"Objective-C Image Info Version", i32 0}
!3 = !{i32 1, !"Objective-C Image Info Section", !"__DATA,__objc_imageinfo,regular,no_dead_strip"}
!4 = !{i32 1, !"Objective-C Garbage Collection", i8 0}
!5 = !{i32 1, !"Objective-C Class Properties", i32 64}
!6 = !{i32 7, !"Dwarf Version", i32 4}
!7 = !{i32 2, !"Debug Info Version", i32 3}
!8 = !{i32 1, !"wchar_size", i32 4}
!9 = !{i32 7, !"PIC Level", i32 2}
!10 = !{i32 7, !"uwtable", i32 2}
!11 = !{i32 7, !"frame-pointer", i32 2}
!12 = !{i32 1, !"Swift Version", i32 7}
!13 = !{i32 1, !"Swift ABI Version", i32 7}
!14 = !{i32 1, !"Swift Major Version", i8 5}
!15 = !{i32 1, !"Swift Minor Version", i8 7}
!16 = distinct !DICompileUnit(language: DW_LANG_Swift, file: !17, producer: "Swift version 5.7-dev (LLVM 50f4aff6823ead1, Swift 7eff6ccfec7c819)", isOptimized: true, runtimeVersion: 5, emissionKind: FullDebug, globals: !18, imports: !19, sysroot: "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX12.3.sdk", sdk: "MacOSX12.3.sdk")
!17 = !DIFile(filename: "/swift/stdlib/public/Concurrency/Actor.swift", directory: "/")
!18 = !{}
!19 = !{}
!20 = !DIImportedEntity(tag: DW_TAG_imported_module, scope: !17, entity: !21, file: !17)
!21 = !DIModule(scope: null, name: "_Concurrency", includePath: "/Users/aschwaighofer/github/swift/stdlib/public/Concurrency")
!125 = !{!"standard-library", i1 false}
!883 = !{!"-lswiftCore"}
!884 = !{!"-lobjc"}
!885 = distinct !DISubprogram(name: "next", linkageName: "$sScG8IteratorV4nextxSgyYaFTY2_", scope: !886, file: !17, line: 765, type: !887, scopeLine: 767, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !16, retainedNodes: !898)
!886 = !DICompositeType(tag: DW_TAG_structure_type, name: "Iterator", scope: !21, file: !17, flags: DIFlagFwdDecl, runtimeLang: DW_LANG_Swift)
!887 = !DISubroutineType(types: !888)
!888 = !{!889, !897}
!889 = !DICompositeType(tag: DW_TAG_structure_type, scope: !21, file: !890, elements: !891, runtimeLang: DW_LANG_Swift)
!890 = !DIFile(filename: "build-rebranch/swift-macosx-arm64/lib/swift/macosx/Swift.swiftmodule/x86_64-apple-macos.swiftmodule", directory: "/")
!891 = !{!892}
!892 = !DIDerivedType(tag: DW_TAG_member, scope: !21, file: !890, baseType: !893)
!893 = !DICompositeType(tag: DW_TAG_structure_type, name: "Optional", scope: !21, file: !890, flags: DIFlagFwdDecl, runtimeLang: DW_LANG_Swift, templateParams: !894, identifier: "$sxSgD")
!894 = !{!895}
!895 = !DITemplateTypeParameter(type: !896)
!896 = !DICompositeType(tag: DW_TAG_structure_type, name: "$sxD", file: !17, runtimeLang: DW_LANG_Swift, identifier: "$sxD")
!897 = !DICompositeType(tag: DW_TAG_structure_type, name: "Iterator", scope: !21, file: !17, size: 72, elements: !18, runtimeLang: DW_LANG_Swift, identifier: "$sScG8IteratorVyx_GD")
!898 = !{!899, !902}
!899 = !DILocalVariable(name: "$\CF\84_0_0", scope: !885, file: !17, type: !900, flags: DIFlagArtificial)
!900 = !DIDerivedType(tag: DW_TAG_typedef, name: "ChildTaskResult", scope: !21, file: !17, baseType: !901)
!901 = !DIDerivedType(tag: DW_TAG_pointer_type, name: "$sBpD", baseType: null, size: 64)
!902 = !DILocalVariable(name: "self", arg: 1, scope: !885, file: !17, line: 765, type: !897, flags: DIFlagArtificial)
!903 = !DILocation(line: 765, column: 26, scope: !885)
!904 = !DILocation(line: 0, scope: !885)
!905 = !DILocation(line: 0, scope: !906)
!906 = !DILexicalBlockFile(scope: !907, file: !17, discriminator: 0)
!907 = distinct !DILexicalBlock(scope: !908, file: !17, line: 767, column: 7)
!908 = distinct !DILexicalBlock(scope: !885, file: !17, line: 765, column: 51)
!909 = !DILocation(line: 767, column: 39, scope: !907)
!910 = !{i64 96}
!911 = !DILocation(line: 771, column: 14, scope: !912)
!912 = distinct !DILexicalBlock(scope: !907, file: !17, line: 767, column: 33)
!913 = !DILocation(line: 767, column: 39, scope: !912)
!914 = !DILocation(line: 771, column: 7, scope: !912)
!915 = !DILocation(line: 766, column: 14, scope: !908)
!916 = !DILocation(line: 0, scope: !917)
!917 = !DILexicalBlockFile(scope: !912, file: !17, discriminator: 0)
!918 = !DILocation(line: 768, column: 18, scope: !919)
!919 = distinct !DILexicalBlock(scope: !908, file: !17, line: 767, column: 51)
!920 = !DILocation(line: 769, column: 16, scope: !919)
!921 = !DILocation(line: 769, column: 9, scope: !919)
!922 = !DILocation(line: 772, column: 5, scope: !912)
!923 = !DILocation(line: 0, scope: !924, inlinedAt: !926)
!924 = distinct !DISubprogram(linkageName: "$sScG8IteratorV4nextxSgyYaF", scope: !21, file: !17, type: !925, flags: DIFlagArtificial, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !16, retainedNodes: !18)
!925 = !DISubroutineType(types: null)
!926 = distinct !DILocation(line: 772, column: 5, scope: !912)

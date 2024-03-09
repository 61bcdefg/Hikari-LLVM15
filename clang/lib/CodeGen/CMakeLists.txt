set(LLVM_LINK_COMPONENTS
  Analysis
  BitReader
  BitWriter
  Core
  Coroutines
  Coverage
  Demangle
  Extensions
  FrontendOpenMP
  IPO
  IRReader
  AggressiveInstCombine
  InstCombine
  Instrumentation
  LTO
  Linker
  MC
  ObjCARCOpts
  Object
  Passes
  ProfileData
  Remarks
  ScalarOpts
  Support
  Target
  TransformUtils
  )

# Workaround for MSVC ARM64 performance regression:
# https://developercommunity.visualstudio.com/t/Compiling-a-specific-code-for-ARM64-with/10444970
# Since /O1 and /O2 represent a set of optimizations,
# our goal is to disable the /Og flag while retaining the other optimizations from the /O1|/O2 set
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES Clang
    AND MSVC_VERSION VERSION_GREATER_EQUAL 1932
    AND CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")

  string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)
  string(REGEX MATCHALL "/[Oo][12]" opt_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${uppercase_CMAKE_BUILD_TYPE}}")
  if (opt_flags)
    if(opt_flags MATCHES "1$")
      set(opt_flags "/Od;/Os;/Oy;/Ob2;/GF;/Gy")
    elseif (opt_flags MATCHES "2$")
      set(opt_flags "/Od;/Oi;/Ot;/Oy;/Ob2;/GF;/Gy")
    endif()
    set_source_files_properties(CGBuiltin.cpp PROPERTIES COMPILE_OPTIONS "${opt_flags}")
  endif()
endif()

add_clang_library(clangCodeGen
  BackendUtil.cpp
  CGAtomic.cpp
  CGBlocks.cpp
  CGBuiltin.cpp
  CGCUDANV.cpp
  CGCUDARuntime.cpp
  CGCXX.cpp
  CGCXXABI.cpp
  CGCall.cpp
  CGClass.cpp
  CGCleanup.cpp
  CGCoroutine.cpp
  CGDebugInfo.cpp
  CGDecl.cpp
  CGDeclCXX.cpp
  CGException.cpp
  CGExpr.cpp
  CGExprAgg.cpp
  CGExprCXX.cpp
  CGExprComplex.cpp
  CGExprConstant.cpp
  CGExprScalar.cpp
  CGGPUBuiltin.cpp
  CGHLSLRuntime.cpp
  CGLoopInfo.cpp
  CGNonTrivialStruct.cpp
  CGObjC.cpp
  CGObjCGNU.cpp
  CGObjCMac.cpp
  CGObjCRuntime.cpp
  CGOpenCLRuntime.cpp
  CGOpenMPRuntime.cpp
  CGOpenMPRuntimeGPU.cpp
  CGPointerAuth.cpp
  CGRecordLayoutBuilder.cpp
  CGStmt.cpp
  CGStmtOpenMP.cpp
  CGVTT.cpp
  CGVTables.cpp
  CodeGenABITypes.cpp
  CodeGenAction.cpp
  CodeGenFunction.cpp
  CodeGenModule.cpp
  CodeGenPGO.cpp
  CodeGenTBAA.cpp
  CodeGenTypes.cpp
  ConstantInitBuilder.cpp
  CoverageMappingGen.cpp
  ItaniumCXXABI.cpp
  MacroPPCallbacks.cpp
  MicrosoftCXXABI.cpp
  ModuleBuilder.cpp
  ObjectFilePCHContainerOperations.cpp
  PatternInit.cpp
  SanitizerMetadata.cpp
  SwiftCallingConv.cpp
  TargetInfo.cpp
  VarBypassDetector.cpp

  DEPENDS
  intrinsics_gen

  LINK_LIBS
  clangAnalysis
  clangAST
  clangBasic
  clangFrontend
  clangLex
  clangSerialization
  )

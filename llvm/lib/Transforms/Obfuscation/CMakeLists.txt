if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    add_llvm_component_library(LLVMObfuscation
            FunctionCallObfuscate.cpp
            CryptoUtils.cpp
            BogusControlFlow.cpp
            SubstituteImpl.cpp
            Substitution.cpp
            Flattening.cpp
            Utils.cpp
            SplitBasicBlocks.cpp
            AntiClassDump.cpp
            AntiDebugging.cpp
            AntiHooking.cpp
            StringEncryption.cpp
            IndirectBranch.cpp
            FunctionWrapper.cpp
            ConstantEncryption.cpp
            Obfuscation.cpp
            DEPENDS
            intrinsics_gen

            LINK_COMPONENTS
            Linker
            )
else()
    add_llvm_component_library(LLVMObfuscation
            FunctionCallObfuscate.cpp
            CryptoUtils.cpp
            BogusControlFlow.cpp
            SubstituteImpl.cpp
            Substitution.cpp
            Flattening.cpp
            Utils.cpp
            SplitBasicBlocks.cpp
            AntiClassDump.cpp
            AntiDebugging.cpp
            AntiHooking.cpp
            StringEncryption.cpp
            IndirectBranch.cpp
            FunctionWrapper.cpp
            ConstantEncryption.cpp
            Obfuscation.cpp
            DEPENDS
            intrinsics_gen
            )
endif()
execute_process(
  COMMAND git log -1 --format=%H
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_VARIABLE HIKARI_GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
target_compile_definitions(LLVMObfuscation PRIVATE "-DGIT_COMMIT_HASH=\"${HIKARI_GIT_COMMIT_HASH}\"")

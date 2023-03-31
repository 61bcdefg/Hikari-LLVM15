include_directories($<TARGET_PROPERTY:LLVMRemoteCacheKVProto,BINARY_DIR>)
include_directories($<TARGET_PROPERTY:LLVMRemoteCacheCASProto,BINARY_DIR>)

set(LLVM_LINK_COMPONENTS
  CAS
  RemoteCacheServer
  Support
)

add_llvm_tool(llvm-remote-cache-test
  llvm-remote-cache-test.cpp
)

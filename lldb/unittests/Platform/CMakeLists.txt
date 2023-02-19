add_lldb_unittest(LLDBPlatformTests
  PlatformAppleSimulatorTest.cpp
  PlatformDarwinTest.cpp
  PlatformMacOSXTest.cpp
  PlatformTest.cpp

  LINK_LIBS
    lldbPluginPlatformMacOSX
  LINK_COMPONENTS
    Support
  )

add_subdirectory(Android)

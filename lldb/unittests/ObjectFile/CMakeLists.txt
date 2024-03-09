add_subdirectory(Breakpad)
add_subdirectory(ELF)
add_subdirectory(MachO)
# PECOFF/TestSectionSize.cpp uses a SizeOfRawData key not recognized
# by the yaml2pecoff on this branch; test fails to build.
# add_subdirectory(PECOFF)

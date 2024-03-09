// Test that optimized flag is properly included in DWARF.

// -gsplit-dwarf is supported only on Linux.
// REQUIRES: system-darwin

// This test uses lldb's embedded python interpreter
// REQUIRES: python

// RUN: %clang_host %s -g -gdwarf-5 -c -o %t.o
// RUN: %clang_host %t.o -o %t.exe
// RUN: %lldb %t.exe -b -o 'b main' -o r -o q 2>&1 | FileCheck %s

// CHECK: error: This version of LLDB does not support DWARF version 5 or later.

int main(void) { return 0; }

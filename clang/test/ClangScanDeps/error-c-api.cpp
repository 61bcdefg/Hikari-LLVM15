// RUN: not c-index-test core -scan-deps %S -- clang_tool %s -I %S/Inputs 2>&1 | FileCheck %s

#include "missing.h"

// CHECK: error: failed to get dependencies
// CHECK-NEXT: 'missing.h' file not found

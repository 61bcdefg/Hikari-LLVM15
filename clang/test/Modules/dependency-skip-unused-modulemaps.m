// RUN: rm -rf %t.cache %t.d %t.cmd
// RUN: %clang_cc1 -fsyntax-only -fmodules -fimplicit-module-maps -fmodules-cache-path=%t.cache -dependency-file %t.d -MT dependencies -I%S/Inputs/dependency-skip-unused/x -I%S/Inputs/dependency-skip-unused/y -I%S/Inputs/dependency-skip-unused -skip-unused-modulemap-deps %s
// RUN: FileCheck %s < %t.d
// CHECK-NOT: dependency-skip-unused{{.}}x{{.}}module.modulemap
// CHECK-NOT: dependency-skip-unused{{.}}y{{.}}module.modulemap

// RUN: %clang -fsyntax-only -fmodules -fimplicit-module-maps -fmodules-cache-path=%t.cache -MMD -MT dependencies -MF %t.d -I%S/Inputs/dependency-skip-unused/x -I%S/Inputs/dependency-skip-unused/y -I%S/Inputs/dependency-skip-unused %s -### 2>&1 | FileCheck -check-prefix=CC1_INV %s
// CC1_INV: -skip-unused-modulemap-deps

@import A;

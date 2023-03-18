// NOTE: Assertions have been autogenerated by utils/update_cc_test_checks.py
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +bf16 -target-feature +f64mm -fallow-half-arguments-and-returns -S -O1 -Werror -Wall -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +bf16 -target-feature +f64mm -fallow-half-arguments-and-returns -S -O1 -Werror -Wall -emit-llvm -o - -x c++ %s | FileCheck %s -check-prefix=CPP-CHECK
// RUN: %clang_cc1 -DSVE_OVERLOADED_FORMS -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +bf16 -target-feature +f64mm -fallow-half-arguments-and-returns -S -O1 -Werror -Wall -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -DSVE_OVERLOADED_FORMS -triple aarch64-none-linux-gnu -target-feature +sve -target-feature +bf16 -target-feature +f64mm -fallow-half-arguments-and-returns -S -O1 -Werror -Wall -emit-llvm -o - -x c++ %s | FileCheck %s -check-prefix=CPP-CHECK

// REQUIRES: aarch64-registered-target || arm-registered-target

#include <arm_sve.h>

#ifdef SVE_OVERLOADED_FORMS
// A simple used,unused... macro, long enough to represent any SVE builtin.
#define SVE_ACLE_FUNC(A1, A2_UNUSED, A3, A4_UNUSED) A1##A3
#else
#define SVE_ACLE_FUNC(A1, A2, A3, A4) A1##A2##A3##A4
#endif

// CHECK-LABEL: @test_svld1ro_bf16(
// CHECK-NEXT:  entry:
// CHECK-NEXT:    [[TMP0:%.*]] = call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1(<vscale x 16 x i1> [[PG:%.*]])
// CHECK-NEXT:    [[TMP1:%.*]] = call <vscale x 8 x bfloat> @llvm.aarch64.sve.ld1ro.nxv8bf16(<vscale x 8 x i1> [[TMP0]], bfloat* [[BASE:%.*]])
// CHECK-NEXT:    ret <vscale x 8 x bfloat> [[TMP1]]
//
// CPP-CHECK-LABEL: @_Z17test_svld1ro_bf16u10__SVBool_tPKu6__bf16(
// CPP-CHECK-NEXT:  entry:
// CPP-CHECK-NEXT:    [[TMP0:%.*]] = call <vscale x 8 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv8i1(<vscale x 16 x i1> [[PG:%.*]])
// CPP-CHECK-NEXT:    [[TMP1:%.*]] = call <vscale x 8 x bfloat> @llvm.aarch64.sve.ld1ro.nxv8bf16(<vscale x 8 x i1> [[TMP0]], bfloat* [[BASE:%.*]])
// CPP-CHECK-NEXT:    ret <vscale x 8 x bfloat> [[TMP1]]
//
svbfloat16_t test_svld1ro_bf16(svbool_t pg, const bfloat16_t *base) {
  return SVE_ACLE_FUNC(svld1ro, _bf16, , )(pg, base);
}
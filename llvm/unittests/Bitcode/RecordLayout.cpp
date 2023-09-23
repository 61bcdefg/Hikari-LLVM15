//===- llvm/unittest/Bitcode/RecordLayout.cpp - Tests for BCRecordLayout --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeConvenience.h"
#include "llvm/Bitstream/BitCodes.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

namespace {
class NonCopyableArrayLikeType {
  int singleElement = 0;
public:
  NonCopyableArrayLikeType() = default;

  NonCopyableArrayLikeType(const NonCopyableArrayLikeType &) = delete;
  NonCopyableArrayLikeType(NonCopyableArrayLikeType &&) = delete;
  void operator=(const NonCopyableArrayLikeType &) = delete;
  void operator=(NonCopyableArrayLikeType &&) = delete;
  ~NonCopyableArrayLikeType() = default;

  const int *begin() const {
    return &singleElement;
  }
  const int *end() const {
    return begin() + 1;
  }
};
} // end anonymous namespace

// This "test" isn't actually run; we just want to make sure it compiles.
LLVM_ATTRIBUTE_UNUSED
static void testThatArrayIsNotCopied(BitstreamWriter &out,
                                     SmallVectorImpl<uint64_t> &buffer) {
  using Layout = BCRecordLayout</*ID*/0, BCFixed<16>, BCArray<BCFixed<16>>>;
  Layout layout(out);

  NonCopyableArrayLikeType varArray;
  layout.emit(buffer, /*field*/1, varArray);

  const NonCopyableArrayLikeType constVarArray;
  layout.emit(buffer, /*field*/1, constVarArray);

  layout.emit(buffer, /*field*/1, NonCopyableArrayLikeType());
}

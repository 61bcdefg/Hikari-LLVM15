//===- CASOptionsTest.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CAS/CASOptions.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "llvm/CAS/ObjectStore.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Testing/Support/Error.h"
#include "llvm/Testing/Support/SupportHelpers.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace clang;
using namespace clang::cas;

namespace {

TEST(CASOptionsTest, getKind) {
  CASOptions Opts;
  EXPECT_EQ(CASOptions::InMemoryCAS, Opts.getKind());

#if LLVM_ENABLE_ONDISK_CAS
  Opts.CASPath = "auto";
  unittest::TempDir Dir("cas-options", /*Unique=*/true);
  EXPECT_EQ(CASOptions::OnDiskCAS, Opts.getKind());

  Opts.CASPath = Dir.path("cas").str().str();
  EXPECT_EQ(CASOptions::OnDiskCAS, Opts.getKind());
#endif
}

TEST(CASOptionsTest, getOrCreateObjectStore) {
  DiagnosticsEngine Diags(new DiagnosticIDs(), new DiagnosticOptions,
                          new IgnoringDiagConsumer());

  // Create an in-memory CAS.
  CASOptions Opts;
  std::shared_ptr<ObjectStore> InMemory = Opts.getOrCreateObjectStore(Diags);
  ASSERT_TRUE(InMemory);
  EXPECT_EQ(InMemory, Opts.getOrCreateObjectStore(Diags));
  EXPECT_EQ(CASOptions::InMemoryCAS, Opts.getKind());

#if LLVM_ENABLE_ONDISK_CAS
  // Create an on-disk CAS.
  unittest::TempDir Dir("cas-options", /*Unique=*/true);
  Opts.CASPath = Dir.path("cas").str().str();
  std::shared_ptr<ObjectStore> OnDisk = Opts.getOrCreateObjectStore(Diags);
  EXPECT_NE(InMemory, OnDisk);
  EXPECT_EQ(OnDisk, Opts.getOrCreateObjectStore(Diags));
  EXPECT_EQ(CASOptions::OnDiskCAS, Opts.getKind());

  // Create an on-disk CAS at an automatic location.
  Opts.CASPath = "auto";
  std::shared_ptr<ObjectStore> OnDiskAuto = Opts.getOrCreateObjectStore(Diags);
  EXPECT_NE(InMemory, OnDiskAuto);
  EXPECT_NE(OnDisk, OnDiskAuto);
  EXPECT_EQ(OnDiskAuto, Opts.getOrCreateObjectStore(Diags));
  EXPECT_EQ(CASOptions::OnDiskCAS, Opts.getKind());

  // Create another in-memory CAS. It won't be the same one.
  Opts.CASPath = "";
  std::shared_ptr<ObjectStore> InMemory2 = Opts.getOrCreateObjectStore(Diags);
  EXPECT_NE(InMemory, InMemory2);
  EXPECT_NE(OnDisk, InMemory2);
  EXPECT_NE(OnDiskAuto, InMemory2);
  EXPECT_EQ(InMemory2, Opts.getOrCreateObjectStore(Diags));
  EXPECT_EQ(CASOptions::InMemoryCAS, Opts.getKind());
#endif
}

#if LLVM_ENABLE_ONDISK_CAS
TEST(CASOptionsTest, getOrCreateObjectStoreInvalid) {
  DiagnosticsEngine Diags(new DiagnosticIDs(), new DiagnosticOptions,
                          new IgnoringDiagConsumer());

  // Create a file, then try to put a CAS there.
  StringRef Contents = "contents";
  unittest::TempDir Dir("cas-options", /*Unique=*/true);
  unittest::TempFile File(Dir.path("cas"), /*Suffix=*/"",
                          /*Contents=*/Contents);

  CASOptions Opts;
  Opts.CASPath = File.path().str();
  EXPECT_EQ(nullptr, Opts.getOrCreateObjectStore(Diags));

  std::shared_ptr<ObjectStore> Empty =
      Opts.getOrCreateObjectStore(Diags, /*CreateEmptyCASOnFailure=*/true);
  EXPECT_EQ(Empty, Opts.getOrCreateObjectStore(Diags));

  // Ensure the file wasn't clobbered.
  std::unique_ptr<MemoryBuffer> MemBuffer;
  ASSERT_THAT_ERROR(
      errorOrToExpected(MemoryBuffer::getFile(File.path())).moveInto(MemBuffer),
      Succeeded());
  ASSERT_EQ(Contents, MemBuffer->getBuffer());
}

TEST(CASOptionsTest, freezeConfig) {
  DiagnosticsEngine Diags(new DiagnosticIDs(), new DiagnosticOptions,
                          new IgnoringDiagConsumer());

  // Hide the CAS configuration when creating it.
  unittest::TempDir Dir("cas-options", /*Unique=*/true);
  CASOptions Opts;
  Opts.CASPath = Dir.path("cas").str().str();
  Opts.freezeConfig(Diags);
  std::shared_ptr<ObjectStore> CAS = Opts.getOrCreateObjectStore(Diags);
  ASSERT_TRUE(CAS);
  EXPECT_EQ(CASOptions::UnknownCAS, Opts.getKind());

  // Check that the configuration is hidden, but calls to
  // getOrCreateObjectStore() still return the original CAS.
  EXPECT_EQ(CAS->getContext().getHashSchemaIdentifier(), Opts.CASPath);

  // Check that new paths are ignored.
  Opts.CASPath = "";
  EXPECT_EQ(CAS, Opts.getOrCreateObjectStore(Diags));
  EXPECT_EQ(CASOptions::UnknownCAS, Opts.getKind());

  Opts.CASPath = Dir.path("ignored-cas").str().str();
  EXPECT_EQ(CAS, Opts.getOrCreateObjectStore(Diags));
  EXPECT_EQ(CASOptions::UnknownCAS, Opts.getKind());
}
#endif

} // end namespace

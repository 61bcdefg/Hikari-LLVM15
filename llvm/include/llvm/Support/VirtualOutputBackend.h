//===- VirtualOutputBackend.h - Output virtualization -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_VIRTUALOUTPUTBACKEND_H
#define LLVM_SUPPORT_VIRTUALOUTPUTBACKEND_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VirtualOutputConfig.h"
#include "llvm/Support/VirtualOutputFile.h"

namespace llvm {
namespace vfs {

/// Interface for virtualized outputs.
///
/// If virtual functions are added here, also add them to \a
/// ProxyOutputBackend.
class OutputBackend : public RefCountedBase<OutputBackend> {
  virtual void anchor();

public:
  /// Get a backend that points to the same destination as this one but that
  /// has independent settings.
  ///
  /// Not thread-safe, but all operations are thread-safe when performed on
  /// separate clones of the same backend.
  IntrusiveRefCntPtr<OutputBackend> clone() const { return cloneImpl(); }

  /// Create a file. If \p Config is \c None, uses the backend's default
  /// OutputConfig (may match \a OutputConfig::OutputConfig(), or may
  /// have been customized).
  ///
  /// Thread-safe.
  Expected<OutputFile> createFile(const Twine &Path,
                                  Optional<OutputConfig> Config = None);

protected:
  /// Must be thread-safe. Virtual function has a different name than \a
  /// clone() so that implementations can override the return value.
  virtual IntrusiveRefCntPtr<OutputBackend> cloneImpl() const = 0;

  /// Create a file for \p Path. Must be thread-safe.
  ///
  /// \pre \p Config is valid or None.
  virtual Expected<std::unique_ptr<OutputFileImpl>>
  createFileImpl(StringRef Path, Optional<OutputConfig> Config) = 0;

  OutputBackend() = default;

public:
  virtual ~OutputBackend() = default;
};

} // namespace vfs
} // namespace llvm

#endif // LLVM_SUPPORT_VIRTUALOUTPUTBACKEND_H

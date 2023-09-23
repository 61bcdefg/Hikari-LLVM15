//===- UnifiedOnDiskCache.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CAS_UNIFIEDONDISKCACHE_H
#define LLVM_CAS_UNIFIEDONDISKCACHE_H

#include "llvm/CAS/OnDiskGraphDB.h"

namespace llvm::cas::ondisk {

class OnDiskKeyValueDB;

/// A unified CAS nodes and key-value database, using on-disk storage for both.
/// It manages storage growth and provides APIs for garbage collection.
///
/// High-level properties:
/// * While \p UnifiedOnDiskCache is open on a directory, by any process, the
///   storage size in that directory will keep growing unrestricted. For data to
///   become eligible for garbase-collection there should be no open instances
///   of \p UnifiedOnDiskCache for that directory, by any process.
/// * Garbage-collection needs to be triggered explicitly by the client. It can
///   be triggered on a directory concurrently, at any time and by any process,
///   without affecting any active readers/writers, in the same process or other
///   processes.
///
/// Usage patterns should be that an instance of \p UnifiedOnDiskCache is open
/// for a limited period of time, e.g. for the duration of a build operation.
/// For long-living processes that need periodic access to a
/// \p UnifiedOnDiskCache, the client should device a scheme where access is
/// performed within some defined period. For example, if a service is designed
/// to continuously wait for requests that access a \p UnifiedOnDiskCache, it
/// could keep the instance alive while new requests are coming in but close it
/// after a time period in which there are no new requests.
class UnifiedOnDiskCache {
public:
  /// The \p OnDiskGraphDB instance for the open directory.
  OnDiskGraphDB &getGraphDB() { return *PrimaryGraphDB; }

  /// Associate an \p ObjectID, of the \p OnDiskGraphDB instance, with a key.
  ///
  /// \param Key the hash bytes for the key.
  /// \param Value the \p ObjectID value.
  ///
  /// \returns the \p ObjectID associated with the \p Key. It may be different
  /// than \p Value if another value was already associated with this key.
  Expected<ObjectID> KVPut(ArrayRef<uint8_t> Key, ObjectID Value);

  /// Associate an \p ObjectID, of the \p OnDiskGraphDB instance, with a key.
  /// An \p ObjectID as a key is equivalent to its digest bytes.
  ///
  /// \param Key the \p ObjectID for the key.
  /// \param Value the \p ObjectID value.
  ///
  /// \returns the \p ObjectID associated with the \p Key. It may be different
  /// than \p Value if another value was already associated with this key.
  Expected<ObjectID> KVPut(ObjectID Key, ObjectID Value);

  /// \returns the \p ObjectID, of the \p OnDiskGraphDB instance, associated
  /// with the \p Key, or \p std::nullopt if the key does not exist.
  Expected<std::optional<ObjectID>> KVGet(ArrayRef<uint8_t> Key);

  /// Open a \p UnifiedOnDiskCache instance for a directory.
  ///
  /// \param Path directory for the on-disk database. The directory will be
  /// created if it doesn't exist.
  /// \param SizeLimit Optional size for limiting growth. This has an effect for
  /// when the instance is closed.
  /// \param HashName Identifier name for the hashing algorithm that is going to
  /// be used.
  /// \param HashByteSize Size for the object digest hash bytes.
  /// \param FaultInPolicy Controls how nodes are copied to primary store. This
  /// is recorded at creation time and subsequent opens need to pass the same
  /// policy otherwise the \p open will fail.
  static Expected<std::unique_ptr<UnifiedOnDiskCache>>
  open(StringRef Path, std::optional<uint64_t> SizeLimit, StringRef HashName,
       unsigned HashByteSize,
       OnDiskGraphDB::FaultInPolicy FaultInPolicy =
           OnDiskGraphDB::FaultInPolicy::FullTree);

  /// This is called implicitly at destruction time, so it is not required for a
  /// client to call this. After calling \p close the only method that is valid
  /// to call is \p needsGarbaseCollection.
  ///
  /// \param CheckSizeLimit if true it will check whether the primary store has
  /// exceeded its intended size limit. If false the check is skipped even if a
  /// \p SizeLimit was passed to the \p open call.
  Error close(bool CheckSizeLimit = true);

  /// \returns whether the primary store has exceeded the intended size limit.
  /// This can return false even if the overall size of the opened directory is
  /// over the \p SizeLimit passed to \p open. To know whether garbage
  /// collection needs to be triggered or not, call \p needsGarbaseCollection.
  bool hasExceededSizeLimit() const;

  /// \returns whether there are unused data that can be deleted using a
  /// \p collectGarbage call.
  bool needsGarbaseCollection() const { return NeedsGarbageCollection; }

  /// Remove any unused data from the directory at \p Path. If there are no such
  /// data the operation is a no-op.
  ///
  /// This can be called concurrently, regardless of whether there is an open
  /// \p UnifiedOnDiskCache instance or not; it has no effect on readers/writers
  /// in the same process or other processes.
  ///
  /// It is recommended that garbage-collection is triggered concurrently in the
  /// background, so that it has minimal effect on the workload of the process.
  static Error collectGarbage(StringRef Path);

  ~UnifiedOnDiskCache();

private:
  UnifiedOnDiskCache();

  Expected<std::optional<ObjectID>>
  faultInFromUpstreamKV(ArrayRef<uint8_t> Key);

  std::string RootPath;
  std::optional<uint64_t> SizeLimit;

  int LockFD = -1;

  std::atomic<bool> NeedsGarbageCollection;
  std::string PrimaryDBDir;

  OnDiskGraphDB *UpstreamGraphDB = nullptr;
  std::unique_ptr<OnDiskGraphDB> PrimaryGraphDB;

  std::unique_ptr<OnDiskKeyValueDB> UpstreamKVDB;
  std::unique_ptr<OnDiskKeyValueDB> PrimaryKVDB;
};

} // namespace llvm::cas::ondisk

#endif // LLVM_CAS_UNIFIEDONDISKCACHE_H

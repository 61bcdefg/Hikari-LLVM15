//===- OnDiskCAS.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BuiltinCAS.h"
#include "llvm/CAS/OnDiskGraphDB.h"
#include "llvm/CAS/UnifiedOnDiskCache.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace llvm::cas;
using namespace llvm::cas::builtin;

namespace {

class OnDiskCAS : public BuiltinCAS {
public:
  Expected<ObjectRef> storeImpl(ArrayRef<uint8_t> ComputedHash,
                                ArrayRef<ObjectRef> Refs,
                                ArrayRef<char> Data) final;

  Expected<std::optional<ObjectHandle>> loadIfExists(ObjectRef Ref) final;

  CASID getID(ObjectRef Ref) const final;

  Optional<ObjectRef> getReference(const CASID &ID) const final;

  Expected<bool> isMaterialized(ObjectRef Ref) const final;

  ArrayRef<char> getDataConst(ObjectHandle Node) const final;

  void print(raw_ostream &OS) const final;

  static Expected<std::unique_ptr<OnDiskCAS>> open(StringRef Path);

  OnDiskCAS(std::shared_ptr<ondisk::UnifiedOnDiskCache> UniDB_)
      : UniDB(std::move(UniDB_)), DB(&UniDB->getGraphDB()) {}

private:
  ObjectHandle convertHandle(ondisk::ObjectHandle Node) const {
    return makeObjectHandle(Node.getOpaqueData());
  }

  ondisk::ObjectHandle convertHandle(ObjectHandle Node) const {
    return ondisk::ObjectHandle::fromOpaqueData(Node.getInternalRef(*this));
  }

  ObjectRef convertRef(ondisk::ObjectID Ref) const {
    return makeObjectRef(Ref.getOpaqueData());
  }

  ondisk::ObjectID convertRef(ObjectRef Ref) const {
    return ondisk::ObjectID::fromOpaqueData(Ref.getInternalRef(*this));
  }

  size_t getNumRefs(ObjectHandle Node) const final {
    auto RefsRange = DB->getObjectRefs(convertHandle(Node));
    return std::distance(RefsRange.begin(), RefsRange.end());
  }
  ObjectRef readRef(ObjectHandle Node, size_t I) const final {
    auto RefsRange = DB->getObjectRefs(convertHandle(Node));
    return convertRef(RefsRange.begin()[I]);
  }
  Error forEachRef(ObjectHandle Node,
                   function_ref<Error(ObjectRef)> Callback) const final;

  OnDiskCAS(std::unique_ptr<ondisk::OnDiskGraphDB> DB_)
      : OwnedDB(std::move(DB_)), DB(OwnedDB.get()) {}

  std::unique_ptr<ondisk::OnDiskGraphDB> OwnedDB;
  std::shared_ptr<ondisk::UnifiedOnDiskCache> UniDB;
  ondisk::OnDiskGraphDB *DB;
};

} // end anonymous namespace

void OnDiskCAS::print(raw_ostream &OS) const { DB->print(OS); }

CASID OnDiskCAS::getID(ObjectRef Ref) const {
  ArrayRef<uint8_t> Hash = DB->getDigest(convertRef(Ref));
  return CASID::create(&getContext(), toStringRef(Hash));
}

Optional<ObjectRef> OnDiskCAS::getReference(const CASID &ID) const {
  std::optional<ondisk::ObjectID> ObjID =
      DB->getExistingReference(ID.getHash());
  if (!ObjID)
    return std::nullopt;
  return convertRef(*ObjID);
}

Expected<bool> OnDiskCAS::isMaterialized(ObjectRef ExternalRef) const {
  return DB->containsObject(convertRef(ExternalRef));
}

ArrayRef<char> OnDiskCAS::getDataConst(ObjectHandle Node) const {
  return DB->getObjectData(convertHandle(Node));
}

Expected<std::optional<ObjectHandle>>
OnDiskCAS::loadIfExists(ObjectRef ExternalRef) {
  Expected<std::optional<ondisk::ObjectHandle>> ObjHnd =
      DB->load(convertRef(ExternalRef));
  if (!ObjHnd)
    return ObjHnd.takeError();
  if (!*ObjHnd)
    return std::nullopt;
  return convertHandle(**ObjHnd);
}

Expected<ObjectRef> OnDiskCAS::storeImpl(ArrayRef<uint8_t> ComputedHash,
                                         ArrayRef<ObjectRef> Refs,
                                         ArrayRef<char> Data) {
  SmallVector<ondisk::ObjectID, 64> IDs;
  IDs.reserve(Refs.size());
  for (ObjectRef Ref : Refs) {
    IDs.push_back(convertRef(Ref));
  }

  ondisk::ObjectID StoredID = DB->getReference(ComputedHash);
  if (Error E = DB->store(StoredID, IDs, Data))
    return std::move(E);
  return convertRef(StoredID);
}

Error OnDiskCAS::forEachRef(ObjectHandle Node,
                            function_ref<Error(ObjectRef)> Callback) const {
  auto RefsRange = DB->getObjectRefs(convertHandle(Node));
  for (ondisk::ObjectID Ref : RefsRange) {
    if (Error E = Callback(convertRef(Ref)))
      return E;
  }
  return Error::success();
}

Expected<std::unique_ptr<OnDiskCAS>> OnDiskCAS::open(StringRef AbsPath) {
  Expected<std::unique_ptr<ondisk::OnDiskGraphDB>> DB =
      ondisk::OnDiskGraphDB::open(AbsPath, BuiltinCASContext::getHashName(),
                                  sizeof(HashType));
  if (!DB)
    return DB.takeError();
  return std::unique_ptr<OnDiskCAS>(new OnDiskCAS(std::move(*DB)));
}

bool cas::isOnDiskCASEnabled() {
#if LLVM_ENABLE_ONDISK_CAS
  return true;
#else
  return false;
#endif
}

Expected<std::unique_ptr<ObjectStore>> cas::createOnDiskCAS(const Twine &Path) {
#if LLVM_ENABLE_ONDISK_CAS
  // FIXME: An absolute path isn't really good enough. Should open a directory
  // and use openat() for files underneath.
  SmallString<256> AbsPath;
  Path.toVector(AbsPath);
  sys::fs::make_absolute(AbsPath);

  // FIXME: Remove this and update clients to do this logic.
  if (AbsPath == getDefaultOnDiskCASStableID())
    AbsPath = StringRef(getDefaultOnDiskCASPath());

  return OnDiskCAS::open(AbsPath);
#else
  return createStringError(inconvertibleErrorCode(), "OnDiskCAS is disabled");
#endif /* LLVM_ENABLE_ONDISK_CAS */
}

std::unique_ptr<ObjectStore>
cas::builtin::createObjectStoreFromUnifiedOnDiskCache(
    std::shared_ptr<ondisk::UnifiedOnDiskCache> UniDB) {
  return std::make_unique<OnDiskCAS>(std::move(UniDB));
}

static constexpr StringLiteral DefaultName = "cas";

void cas::getDefaultOnDiskCASStableID(SmallVectorImpl<char> &Path) {
  Path.assign(DefaultDirProxy.begin(), DefaultDirProxy.end());
  llvm::sys::path::append(Path, DefaultDir, DefaultName);
}

std::string cas::getDefaultOnDiskCASStableID() {
  SmallString<128> Path;
  getDefaultOnDiskCASStableID(Path);
  return Path.str().str();
}

void cas::getDefaultOnDiskCASPath(SmallVectorImpl<char> &Path) {
  // FIXME: Should this return 'Error' instead of hard-failing?
  if (!llvm::sys::path::cache_directory(Path))
    report_fatal_error("cannot get default cache directory");
  llvm::sys::path::append(Path, DefaultDir, DefaultName);
}

std::string cas::getDefaultOnDiskCASPath() {
  SmallString<128> Path;
  getDefaultOnDiskCASPath(Path);
  return Path.str().str();
}

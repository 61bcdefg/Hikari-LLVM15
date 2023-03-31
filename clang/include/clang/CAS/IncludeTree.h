//===- IncludeTree.h - Include-tree CAS graph -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CAS_CASINCLUDETREE_H
#define LLVM_CLANG_CAS_CASINCLUDETREE_H

#include "clang/Basic/SourceManager.h"
#include "llvm/CAS/ObjectStore.h"

namespace llvm {
class SmallBitVector;
namespace vfs {
class FileSystem;
}
}

namespace clang {
namespace cas {

/// Base class for include-tree related nodes. It makes it convenient to
/// add/skip/check the "node kind identifier" (\p getNodeKind()) that is put
/// at the beginning of the object data for every include-tree related node.
template <typename NodeT> class IncludeTreeBase : public ObjectProxy {
protected:
  explicit IncludeTreeBase(ObjectProxy Node) : ObjectProxy(std::move(Node)) {
    assert(isValid(*this));
  }

  StringRef getData() const {
    return ObjectProxy::getData().substr(NodeT::getNodeKind().size());
  }

  static Expected<NodeT> create(ObjectStore &DB, ArrayRef<ObjectRef> Refs,
                                ArrayRef<char> Data);

  static bool isValid(const ObjectProxy &Node) {
    return Node.getData().startswith(NodeT::getNodeKind());
  }

  friend class IncludeFile;
  friend class IncludeFileList;
  friend class IncludeTree;
  friend class IncludeTreeRoot;
};

/// Represents a \p SourceManager file (or buffer in the case of preprocessor
/// predefines) that got included by the preprocessor.
class IncludeFile : public IncludeTreeBase<IncludeFile> {
public:
  static constexpr StringRef getNodeKind() { return "File"; }

  ObjectRef getFilenameRef() const { return getReference(0); }
  ObjectRef getContentsRef() const { return getReference(1); }

  Expected<ObjectProxy> getFilename() {
    return getCAS().getProxy(getFilenameRef());
  }

  Expected<ObjectProxy> getContents() {
    return getCAS().getProxy(getContentsRef());
  }

  struct FileInfo {
    StringRef Filename;
    StringRef Contents;
  };

  Expected<FileInfo> getFileInfo() {
    auto Filename = getFilename();
    if (!Filename)
      return Filename.takeError();
    auto Contents = getContents();
    if (!Contents)
      return Contents.takeError();
    return FileInfo{Filename->getData(), Contents->getData()};
  }

  static Expected<IncludeFile> create(ObjectStore &DB, StringRef Filename,
                                      ObjectRef Contents);

  llvm::Error print(llvm::raw_ostream &OS, unsigned Indent = 0);

  static bool isValid(const ObjectProxy &Node) {
    if (!IncludeTreeBase::isValid(Node))
      return false;
    IncludeTreeBase Base(Node);
    return Base.getNumReferences() == 2 && Base.getData().empty();
  }
  static bool isValid(ObjectStore &DB, ObjectRef Ref) {
    auto Node = DB.getProxy(Ref);
    if (!Node) {
      llvm::consumeError(Node.takeError());
      return false;
    }
    return isValid(*Node);
  }

private:
  friend class IncludeTreeBase<IncludeFile>;
  friend class IncludeFileList;
  friend class IncludeTree;
  friend class IncludeTreeRoot;

  explicit IncludeFile(ObjectProxy Node) : IncludeTreeBase(std::move(Node)) {
    assert(isValid(*this));
  }
};

/// Represents a DAG of included files by the preprocessor.
/// Each node in the DAG represents a particular inclusion of a file that
/// encompasses inclusions of other files as sub-trees, along with all the
/// \p __has_include() preprocessor checks that occurred during preprocessing
/// of that file.
class IncludeTree : public IncludeTreeBase<IncludeTree> {
public:
  static constexpr StringRef getNodeKind() { return "Tree"; }

  Expected<IncludeFile> getBaseFile() {
    auto Node = getCAS().getProxy(getBaseFileRef());
    if (!Node)
      return Node.takeError();
    return IncludeFile(std::move(*Node));
  }

  /// The include file that resulted in this include-tree.
  ObjectRef getBaseFileRef() const { return getReference(0); }

  Expected<IncludeFile::FileInfo> getBaseFileInfo() {
    auto File = getBaseFile();
    if (!File)
      return File.takeError();
    return File->getFileInfo();
  }

  SrcMgr::CharacteristicKind getFileCharacteristic() const {
    return (SrcMgr::CharacteristicKind)dataSkippingIncludes().front();
  }

  size_t getNumIncludes() const { return getNumReferences() - 1; }

  ObjectRef getIncludeRef(size_t I) const {
    assert(I < getNumIncludes());
    return getReference(I + 1);
  }

  /// The sub-include-trees of included files, in the order that they occurred.
  Expected<IncludeTree> getInclude(size_t I) {
    return getInclude(getIncludeRef(I));
  }

  /// The source byte offset for a particular include, pointing to the beginning
  /// of the line just after the #include directive. The offset represents the
  /// location at which point the include-tree would have been processed by the
  /// preprocessor and parser.
  ///
  /// For example:
  /// \code
  ///   #include "a.h" -> include-tree("a.h")
  ///   | <- include-tree-offset("a.h")
  /// \endcode
  ///
  /// Using the "after #include" offset makes it trivial to identify the part
  /// of the source file that encompasses a set of include-trees (for example in
  /// the case where want to identify the "includes preamble" of the main file.
  uint32_t getIncludeOffset(size_t I) const;

  /// The \p __has_include() preprocessor checks, in the order that they
  /// occurred. The source offsets for the checks are not tracked, "replaying"
  /// the include-tree depends on the invariant that the same exact checks will
  /// occur in the same order.
  bool getCheckResult(size_t I) const;

  /// Passes pairs of (IncludeTree, include offset) to \p Callback.
  llvm::Error forEachInclude(
      llvm::function_ref<llvm::Error(std::pair<IncludeTree, uint32_t>)>
          Callback);

  static Expected<IncludeTree>
  create(ObjectStore &DB, SrcMgr::CharacteristicKind FileCharacteristic,
         ObjectRef BaseFile, ArrayRef<std::pair<ObjectRef, uint32_t>> Includes,
         llvm::SmallBitVector Checks);

  static Expected<IncludeTree> get(ObjectStore &DB, ObjectRef Ref);

  llvm::Error print(llvm::raw_ostream &OS, unsigned Indent = 0);

private:
  friend class IncludeTreeBase<IncludeTree>;
  friend class IncludeTreeRoot;

  explicit IncludeTree(ObjectProxy Node) : IncludeTreeBase(std::move(Node)) {
    assert(isValid(*this));
  }

  Expected<IncludeTree> getInclude(ObjectRef Ref) {
    auto Node = getCAS().getProxy(Ref);
    if (!Node)
      return Node.takeError();
    return IncludeTree(std::move(*Node));
  }

  StringRef dataSkippingIncludes() const {
    return getData().drop_front(getNumIncludes() * sizeof(uint32_t));
  }

  static bool isValid(const ObjectProxy &Node);
  static bool isValid(ObjectStore &DB, ObjectRef Ref) {
    auto Node = DB.getProxy(Ref);
    if (!Node) {
      llvm::consumeError(Node.takeError());
      return false;
    }
    return isValid(*Node);
  }
};

/// A flat list of \p IncludeFile entries. This is used along with a simple
/// implementation of a \p vfs::FileSystem produced via
/// \p createIncludeTreeFileSystem().
class IncludeFileList : public IncludeTreeBase<IncludeFileList> {
public:
  static constexpr StringRef getNodeKind() { return "List"; }

  using FileSizeTy = uint32_t;

  size_t getNumFiles() const { return getNumReferences(); }

  ObjectRef getFileRef(size_t I) const {
    assert(I < getNumFiles());
    return getReference(I);
  }

  Expected<IncludeFile> getFile(size_t I) { return getFile(getFileRef(I)); }
  FileSizeTy getFileSize(size_t I) const;

  /// \returns each \p IncludeFile entry along with its file size.
  llvm::Error forEachFile(
      llvm::function_ref<llvm::Error(IncludeFile, FileSizeTy)> Callback);

  /// We record the file size as well to avoid needing to materialize the
  /// underlying buffer for the \p IncludeTreeFileSystem::status()
  /// implementation to provide the file size.
  struct FileEntry {
    ObjectRef FileRef;
    FileSizeTy Size;
  };
  static Expected<IncludeFileList> create(ObjectStore &DB,
                                          ArrayRef<FileEntry> Files);

  static Expected<IncludeFileList> get(ObjectStore &CAS, ObjectRef Ref);

  llvm::Error print(llvm::raw_ostream &OS, unsigned Indent = 0);

private:
  friend class IncludeTreeBase<IncludeFileList>;
  friend class IncludeTreeRoot;

  explicit IncludeFileList(ObjectProxy Node)
      : IncludeTreeBase(std::move(Node)) {
    assert(isValid(*this));
  }

  Expected<IncludeFile> getFile(ObjectRef Ref) {
    auto Node = getCAS().getProxy(Ref);
    if (!Node)
      return Node.takeError();
    return IncludeFile(std::move(*Node));
  }

  static bool isValid(const ObjectProxy &Node);
  static bool isValid(ObjectStore &CAS, ObjectRef Ref) {
    auto Node = CAS.getProxy(Ref);
    if (!Node) {
      llvm::consumeError(Node.takeError());
      return false;
    }
    return isValid(*Node);
  }
};

/// Represents the include-tree result for a translation unit.
class IncludeTreeRoot : public IncludeTreeBase<IncludeTreeRoot> {
public:
  static constexpr StringRef getNodeKind() { return "Root"; }

  ObjectRef getMainFileTreeRef() const { return getReference(0); }

  ObjectRef getFileListRef() const { return getReference(1); }

  Optional<ObjectRef> getPCHRef() const {
    if (getNumReferences() > 2)
      return getReference(2);
    return None;
  }

  Expected<IncludeTree> getMainFileTree() {
    auto Node = getCAS().getProxy(getMainFileTreeRef());
    if (!Node)
      return Node.takeError();
    return IncludeTree(std::move(*Node));
  }

  Expected<IncludeFileList> getFileList() {
    auto Node = getCAS().getProxy(getFileListRef());
    if (!Node)
      return Node.takeError();
    return IncludeFileList(std::move(*Node));
  }

  Expected<Optional<StringRef>> getPCHBuffer() {
    if (Optional<ObjectRef> Ref = getPCHRef()) {
      auto Node = getCAS().getProxy(*Ref);
      if (!Node)
        return Node.takeError();
      return Node->getData();
    }
    return None;
  }

  static Expected<IncludeTreeRoot> create(ObjectStore &DB,
                                          ObjectRef MainFileTree,
                                          ObjectRef FileList,
                                          Optional<ObjectRef> PCHRef);

  static Expected<IncludeTreeRoot> get(ObjectStore &DB, ObjectRef Ref);

  llvm::Error print(llvm::raw_ostream &OS, unsigned Indent = 0);

  static bool isValid(const ObjectProxy &Node) {
    if (!IncludeTreeBase::isValid(Node))
      return false;
    IncludeTreeBase Base(Node);
    return (Base.getNumReferences() == 2 || Base.getNumReferences() == 3) &&
           Base.getData().empty();
  }
  static bool isValid(ObjectStore &DB, ObjectRef Ref) {
    auto Node = DB.getProxy(Ref);
    if (!Node) {
      llvm::consumeError(Node.takeError());
      return false;
    }
    return isValid(*Node);
  }

private:
  friend class IncludeTreeBase<IncludeTreeRoot>;

  explicit IncludeTreeRoot(ObjectProxy Node)
      : IncludeTreeBase(std::move(Node)) {
    assert(isValid(*this));
  }
};

/// An implementation of a \p vfs::FileSystem that supports the simple queries
/// of the preprocessor, for creating \p FileEntries using a file path, while
/// "replaying" an \p IncludeTreeRoot. It is not intended to be a complete
/// implementation of a file system.
Expected<IntrusiveRefCntPtr<llvm::vfs::FileSystem>>
createIncludeTreeFileSystem(IncludeTreeRoot &Root);

} // namespace cas
} // namespace clang

#endif

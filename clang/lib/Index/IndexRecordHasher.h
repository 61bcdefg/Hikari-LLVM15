//===--- IndexRecordHasher.h - Index record hashing -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_INDEX_INDEXRECORDHASHER_H
#define LLVM_CLANG_LIB_INDEX_INDEXRECORDHASHER_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"

namespace clang {
  class ASTContext;
  class Decl;
  class DeclarationName;
  class IdentifierInfo;
  class MacroInfo;
  class NestedNameSpecifier;
  class QualType;
  class Type;
  template <typename> class CanQual;
  typedef CanQual<Type> CanQualType;

namespace index {
  class FileIndexRecord;

class IndexRecordHasher {
  ASTContext &Ctx;
  llvm::DenseMap<const void *, llvm::hash_code> HashByPtr;

public:
  explicit IndexRecordHasher(ASTContext &Ctx) : Ctx(Ctx) {}
  ASTContext &getASTContext() { return Ctx; }

  llvm::hash_code hashRecord(const FileIndexRecord &Record);
  llvm::hash_code hash(const Decl *D);
  llvm::hash_code hash(const IdentifierInfo *Name, const MacroInfo *M);
  llvm::hash_code hash(QualType Ty);
  llvm::hash_code hash(CanQualType Ty);
  llvm::hash_code hash(DeclarationName Name);
  llvm::hash_code hash(const NestedNameSpecifier *NNS);

private:
  template <typename T>
  llvm::hash_code tryCache(const void *Ptr, T Obj);

  struct MacroDef {
    const IdentifierInfo *Name;
    const MacroInfo *MI;
  };

  llvm::hash_code hashImpl(const Decl *D);
  llvm::hash_code hashImpl(MacroDef MD);
  llvm::hash_code hashImpl(CanQualType Ty);
  llvm::hash_code hashImpl(DeclarationName Name);
  llvm::hash_code hashImpl(const NestedNameSpecifier *NNS);
};

} // end namespace index
} // end namespace clang

#endif

//===--- APINotesReader.h - API Notes Reader ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the \c APINotesReader class that reads source
// API notes data providing additional information about source code as
// a separate input, such as the non-nil/nilable annotations for
// method parameters.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_API_NOTES_READER_H
#define LLVM_CLANG_API_NOTES_READER_H

#include "clang/APINotes/Types.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VersionTuple.h"
#include <memory>

namespace clang {
namespace api_notes {

/// A class that reads API notes data from a binary file that was written by
/// the \c APINotesWriter.
class APINotesReader {
  class Implementation;

  Implementation &Impl;

  APINotesReader(llvm::MemoryBuffer *inputBuffer, bool ownsInputBuffer,
                 llvm::VersionTuple swiftVersion, bool &failed);

public:
  /// Create a new API notes reader from the given member buffer, which
  /// contains the contents of a binary API notes file.
  ///
  /// \returns the new API notes reader, or null if an error occurred.
  static std::unique_ptr<APINotesReader>
  get(std::unique_ptr<llvm::MemoryBuffer> inputBuffer,
      llvm::VersionTuple swiftVersion);

  /// Create a new API notes reader from the given member buffer, which
  /// contains the contents of a binary API notes file.
  ///
  /// \returns the new API notes reader, or null if an error occurred.
  static std::unique_ptr<APINotesReader>
  getUnmanaged(llvm::MemoryBuffer *inputBuffer,
               llvm::VersionTuple swiftVersion);

  ~APINotesReader();

  APINotesReader(const APINotesReader &) = delete;
  APINotesReader &operator=(const APINotesReader &) = delete;

  /// Retrieve the name of the module for which this reader is providing API
  /// notes.
  llvm::StringRef getModuleName() const;

  /// Retrieve the size and modification time of the source file from
  /// which this API notes file was created, if known.
  llvm::Optional<std::pair<off_t, time_t>> getSourceFileSizeAndModTime() const;

  /// Retrieve the module options
  ModuleOptions getModuleOptions() const;

  /// Captures the completed versioned information for a particular part of
  /// API notes, including both unversioned API notes and each versioned API
  /// note for that particular entity.
  template<typename T>
  class VersionedInfo {
    /// The complete set of results.
    llvm::SmallVector<std::pair<llvm::VersionTuple, T>, 1> Results;

    /// The index of the result that is the "selected" set based on the desired
    /// Swift version, or \c Results.size() if nothing matched.
    unsigned Selected;

  public:
    /// Form an empty set of versioned information.
    VersionedInfo(llvm::NoneType) : Selected(0) { }
    
    /// Form a versioned info set given the desired version and a set of
    /// results.
    VersionedInfo(llvm::VersionTuple version,
                  llvm::SmallVector<std::pair<llvm::VersionTuple, T>, 1> results);

    /// Determine whether there is a result that should be applied directly
    /// to the AST.
    explicit operator bool() const { return Selected != size(); }

    /// Retrieve the information to apply directly to the AST.
    const T& operator*() const {
      assert(*this && "No result to apply directly");
      return (*this)[Selected].second;
    }

    /// Retrieve the selected index in the result set.
    llvm::Optional<unsigned> getSelected() const {
      if (Selected == Results.size()) return llvm::None;
      return Selected;
    }

    /// Return the number of versioned results we know about.
    unsigned size() const { return Results.size(); }

    /// Access all versioned results.
    const std::pair<llvm::VersionTuple, T> *begin() const { return Results.begin(); }
    const std::pair<llvm::VersionTuple, T> *end() const { return Results.end(); }

    /// Access a specific versioned result.
    const std::pair<llvm::VersionTuple, T> &operator[](unsigned index) const {
      return Results[index];
    }
  };

  /// Look for the context ID of the given Objective-C class.
  ///
  /// \param name The name of the class we're looking for.
  ///
  /// \returns The ID, if known.
  llvm::Optional<ContextID> lookupObjCClassID(llvm::StringRef name);

  /// Look for information regarding the given Objective-C class.
  ///
  /// \param name The name of the class we're looking for.
  ///
  /// \returns The information about the class, if known.
  VersionedInfo<ObjCContextInfo> lookupObjCClassInfo(llvm::StringRef name);

  /// Look for the context ID of the given Objective-C protocol.
  ///
  /// \param name The name of the protocol we're looking for.
  ///
  /// \returns The ID of the protocol, if known.
  llvm::Optional<ContextID> lookupObjCProtocolID(llvm::StringRef name);

  /// Look for information regarding the given Objective-C protocol.
  ///
  /// \param name The name of the protocol we're looking for.
  ///
  /// \returns The information about the protocol, if known.
  VersionedInfo<ObjCContextInfo> lookupObjCProtocolInfo(llvm::StringRef name);

  /// Look for information regarding the given Objective-C property in
  /// the given context.
  ///
  /// \param contextID The ID that references the context we are looking for.
  /// \param name The name of the property we're looking for.
  /// \param isInstance Whether we are looking for an instance property (vs.
  /// a class property).
  ///
  /// \returns Information about the property, if known.
  VersionedInfo<ObjCPropertyInfo> lookupObjCProperty(ContextID contextID,
                                                     llvm::StringRef name,
                                                     bool isInstance);

  /// Look for information regarding the given Objective-C method in
  /// the given context.
  ///
  /// \param contextID The ID that references the context we are looking for.
  /// \param selector The selector naming the method we're looking for.
  /// \param isInstanceMethod Whether we are looking for an instance method.
  ///
  /// \returns Information about the method, if known.
  VersionedInfo<ObjCMethodInfo> lookupObjCMethod(ContextID contextID,
                                                 ObjCSelectorRef selector,
                                                 bool isInstanceMethod);

  /// Look for information regarding the given global variable.
  ///
  /// \param name The name of the global variable.
  ///
  /// \returns information about the global variable, if known.
  VersionedInfo<GlobalVariableInfo>
  lookupGlobalVariable(llvm::StringRef name,
                       std::optional<Context> context = std::nullopt);

  /// Look for information regarding the given global function.
  ///
  /// \param name The name of the global function.
  ///
  /// \returns information about the global function, if known.
  VersionedInfo<GlobalFunctionInfo>
  lookupGlobalFunction(llvm::StringRef name,
                       std::optional<Context> context = std::nullopt);

  /// Look for information regarding the given enumerator.
  ///
  /// \param name The name of the enumerator.
  ///
  /// \returns information about the enumerator, if known.
  VersionedInfo<EnumConstantInfo> lookupEnumConstant(llvm::StringRef name);

  /// Look for information regarding the given tag
  /// (struct/union/enum/C++ class).
  ///
  /// \param name The name of the tag.
  ///
  /// \returns information about the tag, if known.
  VersionedInfo<TagInfo>
  lookupTag(llvm::StringRef name,
            std::optional<Context> context = std::nullopt);

  /// Look for information regarding the given typedef.
  ///
  /// \param name The name of the typedef.
  ///
  /// \returns information about the typedef, if known.
  VersionedInfo<TypedefInfo>
  lookupTypedef(llvm::StringRef name,
                std::optional<Context> context = std::nullopt);

  /// Look for the context ID of the given C++ namespace.
  ///
  /// \param name The name of the class we're looking for.
  ///
  /// \returns The ID, if known.
  llvm::Optional<ContextID>
  lookupNamespaceID(llvm::StringRef name,
                    llvm::Optional<ContextID> parentNamespaceID = llvm::None);
};

} // end namespace api_notes
} // end namespace clang

#endif // LLVM_CLANG_API_NOTES_READER_H

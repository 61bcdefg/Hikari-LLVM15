//===-- SwiftArray.cpp ----------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "SwiftArray.h"

#include "Plugins/LanguageRuntime/Swift/SwiftLanguageRuntime.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "Plugins/TypeSystem/Swift/TypeSystemSwiftTypeRef.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

// FIXME: we should not need this
#include "Plugins/Language/ObjC/Cocoa.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;
using namespace lldb_private::formatters::swift;

size_t SwiftArrayNativeBufferHandler::GetCount() { return m_size; }

size_t SwiftArrayNativeBufferHandler::GetCapacity() { return m_capacity; }

lldb_private::CompilerType SwiftArrayNativeBufferHandler::GetElementType() {
  return m_elem_type;
}

ValueObjectSP SwiftArrayNativeBufferHandler::GetElementAtIndex(size_t idx) {
  if (idx >= m_size)
    return ValueObjectSP();

  lldb::addr_t child_location = m_first_elem_ptr + idx * m_element_stride;

  ProcessSP process_sp(m_exe_ctx_ref.GetProcessSP());
  if (!process_sp)
    return ValueObjectSP();

  WritableDataBufferSP buffer(new DataBufferHeap(m_element_size, 0));
  Status error;
  if (process_sp->ReadMemory(child_location, buffer->GetBytes(), m_element_size,
                             error) != m_element_size ||
      error.Fail())
    return ValueObjectSP();
  DataExtractor data(buffer, process_sp->GetByteOrder(),
                     process_sp->GetAddressByteSize());
  StreamString name;
  name.Printf("[%zu]", idx);
  return ValueObject::CreateValueObjectFromData(name.GetData(), data,
                                                m_exe_ctx_ref, m_elem_type);
}

SwiftArrayNativeBufferHandler::SwiftArrayNativeBufferHandler(
    ValueObject &valobj, lldb::addr_t native_ptr, CompilerType elem_type)
    : m_metadata_ptr(LLDB_INVALID_ADDRESS),
      m_reserved_word(LLDB_INVALID_ADDRESS), m_size(0), m_capacity(0),
      m_first_elem_ptr(LLDB_INVALID_ADDRESS), m_elem_type(elem_type),
      m_element_size(0), m_element_stride(0),
      m_exe_ctx_ref(valobj.GetExecutionContextRef()) {
  if (native_ptr == LLDB_INVALID_ADDRESS)
    return;
  if (native_ptr == 0) {
    // 0 is a valid value for the pointer here - it just means empty
    // never-written-to array
    m_metadata_ptr = 0;
    m_reserved_word = 0;
    m_size = m_capacity = 0;
    m_first_elem_ptr = 0;
    return;
  }
  ProcessSP process_sp(m_exe_ctx_ref.GetProcessSP());
  if (!process_sp)
    return;
  auto opt_size = elem_type.GetByteSize(process_sp.get());
  if (opt_size)
    m_element_size = *opt_size;
  auto opt_stride = elem_type.GetByteStride(process_sp.get());
  if (opt_stride)
    m_element_stride = *opt_stride;
  size_t ptr_size = process_sp->GetAddressByteSize();
  Status error;
  lldb::addr_t next_read = native_ptr;
  m_metadata_ptr = process_sp->ReadPointerFromMemory(next_read, error);
  if (error.Fail())
    return;
  next_read += ptr_size;
  m_reserved_word =
      process_sp->ReadUnsignedIntegerFromMemory(next_read, ptr_size, 0, error);
  if (error.Fail())
    return;
  next_read += ptr_size;
  m_size =
      process_sp->ReadUnsignedIntegerFromMemory(next_read, ptr_size, 0, error);
  if (error.Fail())
    return;
  next_read += ptr_size;
  m_capacity =
      process_sp->ReadUnsignedIntegerFromMemory(next_read, ptr_size, 0, error);
  if (error.Fail())
    return;
  next_read += ptr_size;
  m_first_elem_ptr = next_read;
}

bool SwiftArrayNativeBufferHandler::IsValid() {
  return m_metadata_ptr != LLDB_INVALID_ADDRESS &&
         m_first_elem_ptr != LLDB_INVALID_ADDRESS && m_capacity >= m_size &&
         m_elem_type.IsValid();
}

size_t SwiftArrayBridgedBufferHandler::GetCount() {
  return m_frontend->CalculateNumChildren();
}

size_t SwiftArrayBridgedBufferHandler::GetCapacity() { return GetCount(); }

lldb_private::CompilerType SwiftArrayBridgedBufferHandler::GetElementType() {
  return m_elem_type;
}

lldb::ValueObjectSP
SwiftArrayBridgedBufferHandler::GetElementAtIndex(size_t idx) {
  return m_frontend->GetChildAtIndex(idx);
}

SwiftArrayBridgedBufferHandler::SwiftArrayBridgedBufferHandler(
    ProcessSP process_sp, lldb::addr_t native_ptr)
    : SwiftArrayBufferHandler(), m_elem_type(), m_synth_array_sp(),
      m_frontend(nullptr) {
  TypeSystemClangSP clang_ts_sp =
        ScratchTypeSystemClang::GetForTarget(process_sp->GetTarget());
  if (!clang_ts_sp)
    return;
  m_elem_type = clang_ts_sp->GetBasicType(lldb::eBasicTypeObjCID);
  InferiorSizedWord isw(native_ptr, *process_sp);
  m_synth_array_sp = ValueObjectConstResult::CreateValueObjectFromData(
      "_", isw.GetAsData(process_sp->GetByteOrder()), *process_sp, m_elem_type);
  if ((m_frontend = NSArraySyntheticFrontEndCreator(nullptr, m_synth_array_sp)))
    m_frontend->Update();
}

bool SwiftArrayBridgedBufferHandler::IsValid() {
  return m_synth_array_sp.get() != nullptr && m_frontend != nullptr;
}

size_t SwiftArraySliceBufferHandler::GetCount() { return m_size; }

size_t SwiftArraySliceBufferHandler::GetCapacity() {
  // Slices don't have a separate capacity - at least not in any obvious sense
  return m_size;
}

lldb_private::CompilerType SwiftArraySliceBufferHandler::GetElementType() {
  return m_elem_type;
}

lldb::ValueObjectSP
SwiftArraySliceBufferHandler::GetElementAtIndex(size_t idx) {
  if (idx >= m_size)
    return ValueObjectSP();

  const uint64_t effective_idx = idx + m_start_index;

  lldb::addr_t child_location =
      m_first_elem_ptr + effective_idx * m_element_stride;

  ProcessSP process_sp(m_exe_ctx_ref.GetProcessSP());
  if (!process_sp)
    return ValueObjectSP();

  WritableDataBufferSP buffer(new DataBufferHeap(m_element_size, 0));
  Status error;
  if (process_sp->ReadMemory(child_location, buffer->GetBytes(), m_element_size,
                             error) != m_element_size ||
      error.Fail())
    return ValueObjectSP();
  DataExtractor data(buffer, process_sp->GetByteOrder(),
                     process_sp->GetAddressByteSize());
  StreamString name;
  name.Printf("[%" PRIu64 "]", effective_idx);
  return ValueObject::CreateValueObjectFromData(name.GetData(), data,
                                                m_exe_ctx_ref, m_elem_type);
}

// this gets passed the "buffer" element?
SwiftArraySliceBufferHandler::SwiftArraySliceBufferHandler(
    ValueObject &valobj, CompilerType elem_type)
    : m_size(0), m_first_elem_ptr(LLDB_INVALID_ADDRESS), m_elem_type(elem_type),
      m_element_size(0), m_element_stride(0),
      m_exe_ctx_ref(valobj.GetExecutionContextRef()), m_native_buffer(false),
      m_start_index(0) {
  static ConstString g_start("subscriptBaseAddress");
  static ConstString g_value("_value");
  static ConstString g__rawValue("_rawValue");
  static ConstString g__countAndFlags("endIndexAndFlags");
  static ConstString g__startIndex("startIndex");

  ProcessSP process_sp(m_exe_ctx_ref.GetProcessSP());
  if (!process_sp)
    return;

  auto opt_size = elem_type.GetByteSize(process_sp.get());
  if (opt_size)
    m_element_size = *opt_size;

  auto opt_stride = elem_type.GetByteStride(process_sp.get());
  if (opt_stride)
    m_element_stride = *opt_stride;

  ValueObjectSP value_sp(valobj.GetChildAtNamePath({g_start, g__rawValue}));
  if (!value_sp)
    return;

  m_first_elem_ptr = value_sp->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);

  ValueObjectSP _countAndFlags_sp(
      valobj.GetChildAtNamePath({g__countAndFlags, g_value}));

  if (!_countAndFlags_sp)
    return;

  ValueObjectSP startIndex_sp(
      valobj.GetChildAtNamePath({g__startIndex, g_value}));

  if (startIndex_sp)
    m_start_index = startIndex_sp->GetValueAsUnsigned(0);

  InferiorSizedWord isw(_countAndFlags_sp->GetValueAsUnsigned(0), *process_sp);

  m_size = (isw >> 1).GetValue() - m_start_index;

  m_native_buffer = !((isw & 1).IsZero());
}

bool SwiftArraySliceBufferHandler::IsValid() {
  return m_first_elem_ptr != LLDB_INVALID_ADDRESS && m_elem_type.IsValid();
}

size_t SwiftSyntheticFrontEndBufferHandler::GetCount() {
  return m_frontend->CalculateNumChildren();
}

size_t SwiftSyntheticFrontEndBufferHandler::GetCapacity() {
  return m_frontend->CalculateNumChildren();
}

lldb_private::CompilerType
SwiftSyntheticFrontEndBufferHandler::GetElementType() {
  // this doesn't make sense here - the synthetic children know best
  return CompilerType();
}

lldb::ValueObjectSP
SwiftSyntheticFrontEndBufferHandler::GetElementAtIndex(size_t idx) {
  return m_frontend->GetChildAtIndex(idx);
}

// this receives a pointer to the NSArray
SwiftSyntheticFrontEndBufferHandler::SwiftSyntheticFrontEndBufferHandler(
    ValueObjectSP valobj_sp)
    : m_valobj_sp(valobj_sp),
      m_frontend(NSArraySyntheticFrontEndCreator(nullptr, valobj_sp)) {
  // Cocoa NSArray frontends must be updated before use
  if (m_frontend)
    m_frontend->Update();
}

bool SwiftSyntheticFrontEndBufferHandler::IsValid() {
  return m_frontend.get() != nullptr;
}

std::unique_ptr<SwiftArrayBufferHandler>
SwiftArrayBufferHandler::CreateBufferHandler(ValueObject &static_valobj) {
  lldb::ValueObjectSP valobj_sp =
      static_valobj.GetDynamicValue(lldb::eDynamicCanRunTarget);
  ValueObject &valobj = valobj_sp ? *valobj_sp : static_valobj;
  llvm::StringRef valobj_typename(
      valobj.GetCompilerType().GetTypeName().AsCString(""));

  if (!valobj.GetTargetSP())
    return nullptr;
  TypeSystemClangSP clang_ts_sp =
      ScratchTypeSystemClang::GetForTarget(*valobj.GetTargetSP());
  if (!clang_ts_sp)
    return nullptr;

  // For now we have to keep the old mangled name since the Objc->Swift bindings
  // that are in Foundation don't get the new mangling.
  if (valobj_typename.startswith("_TtCs23_ContiguousArrayStorage") ||
      valobj_typename.startswith("Swift._ContiguousArrayStorage")) {
    CompilerType anyobject_type = clang_ts_sp->GetBasicType(
            lldb::eBasicTypeObjCID);
    auto handler = std::unique_ptr<SwiftArrayBufferHandler>(
        new SwiftArrayNativeBufferHandler(
            valobj, valobj.GetValueAsUnsigned(LLDB_INVALID_ADDRESS),
            anyobject_type));
    if (handler && handler->IsValid())
      return handler;
    return nullptr;
  }

  if (valobj_typename.startswith("_TtCs22__SwiftDeferredNSArray") ||
      valobj_typename.startswith("Swift.__SwiftDeferredNSArray")) {
    ProcessSP process_sp(valobj.GetProcessSP());
    if (!process_sp)
      return nullptr;
    Status error;

    lldb::addr_t buffer_ptr = valobj.GetValueAsUnsigned(LLDB_INVALID_ADDRESS) +
                              3 * process_sp->GetAddressByteSize();
    buffer_ptr = process_sp->ReadPointerFromMemory(buffer_ptr, error);
    if (error.Fail() || buffer_ptr == LLDB_INVALID_ADDRESS)
      return nullptr;

    lldb::addr_t argmetadata_ptr =
        process_sp->ReadPointerFromMemory(buffer_ptr, error);
    if (error.Fail() || argmetadata_ptr == LLDB_INVALID_ADDRESS)
      return nullptr;

    // Get the type of the array elements.
    CompilerType argument_type;
    auto scratch_ctx_reader = valobj.GetSwiftScratchContext();
    if (!scratch_ctx_reader)
      return nullptr;
    auto *ts = scratch_ctx_reader->get();
    if (!ts)
      return nullptr;
    auto *swift_runtime = SwiftLanguageRuntime::Get(process_sp);
    if (!swift_runtime)
      return nullptr;

    if (CompilerType type =
            swift_runtime->GetTypeFromMetadata(*ts, argmetadata_ptr))
      if (auto ts = type.GetTypeSystem().dyn_cast_or_null<TypeSystemSwift>())
        argument_type = ts->GetGenericArgumentType(type.GetOpaqueQualType(), 0);

    if (!argument_type)
      return nullptr;

    auto handler = std::unique_ptr<SwiftArrayBufferHandler>(
        new SwiftArrayNativeBufferHandler(valobj, buffer_ptr, argument_type));
    if (handler && handler->IsValid())
      return handler;
    return nullptr;
  }

  ExecutionContext exe_ctx(valobj.GetExecutionContextRef());
  ExecutionContextScope *exe_scope = exe_ctx.GetBestExecutionContextScope();
  if (valobj_typename.startswith("Swift.ContiguousArray<")) {
    // Swift.NativeArray
    static ConstString g__buffer("_buffer");
    static ConstString g__storage("_storage");

    ValueObjectSP storage_sp(valobj.GetNonSyntheticValue()->GetChildAtNamePath(
        {g__buffer, g__storage}));

    if (!storage_sp)
      return nullptr;

    CompilerType elem_type(
        valobj.GetCompilerType().GetArrayElementType(exe_scope));

    auto handler = std::unique_ptr<SwiftArrayBufferHandler>(
        new SwiftArrayNativeBufferHandler(
            *storage_sp, storage_sp->GetValueAsUnsigned(LLDB_INVALID_ADDRESS),
            elem_type));
    if (handler && handler->IsValid())
      return handler;
    return nullptr;
  } else if (valobj_typename.startswith("Swift.ArraySlice<") ||
             (valobj_typename.startswith("Swift.Array<") &&
              valobj_typename.endswith(">.SubSequence"))) {
    // ArraySlice or Array<T>.SubSequence, which is a typealias to ArraySlice.
    static ConstString g_buffer("_buffer");

    ValueObjectSP buffer_sp(
        valobj.GetNonSyntheticValue()->GetChildAtNamePath({g_buffer}));
    if (!buffer_sp)
      return nullptr;

    CompilerType elem_type(
        valobj.GetCompilerType().GetArrayElementType(exe_scope));

    auto handler = std::unique_ptr<SwiftArrayBufferHandler>(
        new SwiftArraySliceBufferHandler(*buffer_sp, elem_type));
    if (handler && handler->IsValid())
      return handler;
    return nullptr;
  } else {
    // Swift.Array
    static ConstString g_buffer("_buffer");
    static ConstString g__storage("_storage");
    static ConstString g_rawValue("rawValue");

    ValueObjectSP buffer_sp(valobj.GetNonSyntheticValue()->GetChildAtNamePath(
        {g_buffer, g__storage, g_rawValue}));

    // For the new Array version which uses SIL tail-allocated arrays.
    if (!buffer_sp)
      buffer_sp = valobj.GetNonSyntheticValue()->GetChildAtNamePath(
          {g_buffer, g__storage});

    if (!buffer_sp)
      return nullptr;

    lldb::addr_t storage_location =
        buffer_sp->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);

    if (storage_location != LLDB_INVALID_ADDRESS) {
      ProcessSP process_sp(valobj.GetProcessSP());
      if (!process_sp)
        return nullptr;
      auto *swift_runtime = SwiftLanguageRuntime::Get(process_sp);
      if (!swift_runtime)
        return nullptr;
      lldb::addr_t masked_storage_location =
          swift_runtime->MaskMaybeBridgedPointer(storage_location);

      std::unique_ptr<SwiftArrayBufferHandler> handler;
      if (masked_storage_location == storage_location) {
        CompilerType elem_type(
            valobj.GetCompilerType().GetArrayElementType(exe_scope));
        handler.reset(new SwiftArrayNativeBufferHandler(
            valobj, storage_location, elem_type));
      } else {
        handler.reset(new SwiftArrayBridgedBufferHandler(
            process_sp, masked_storage_location));
      }

      if (handler && handler->IsValid())
        return handler;
      return nullptr;
    } else {
      CompilerType elem_type(
          valobj.GetCompilerType().GetArrayElementType(exe_scope));
      return std::unique_ptr<SwiftArrayBufferHandler>(
          new SwiftArrayEmptyBufferHandler(elem_type));
    }
  }
}

bool lldb_private::formatters::swift::Array_SummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  auto handler = SwiftArrayBufferHandler::CreateBufferHandler(valobj);

  if (!handler)
    return false;

  auto count = handler->GetCount();

  stream.Printf("%zu value%s", count, (count == 1 ? "" : "s"));

  return true;
};

lldb_private::formatters::swift::ArraySyntheticFrontEnd::ArraySyntheticFrontEnd(
    lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp.get()), m_array_buffer() {
  if (valobj_sp)
    Update();
}

size_t lldb_private::formatters::swift::ArraySyntheticFrontEnd::
    CalculateNumChildren() {
  return m_array_buffer ? m_array_buffer->GetCount() : 0;
}

lldb::ValueObjectSP
lldb_private::formatters::swift::ArraySyntheticFrontEnd::GetChildAtIndex(
    size_t idx) {
  if (!m_array_buffer)
    return ValueObjectSP();

  lldb::ValueObjectSP child_sp = m_array_buffer->GetElementAtIndex(idx);
  if (child_sp)
    child_sp->SetSyntheticChildrenGenerated(true);

  return child_sp;
}

bool lldb_private::formatters::swift::ArraySyntheticFrontEnd::Update() {
  m_array_buffer = SwiftArrayBufferHandler::CreateBufferHandler(m_backend);
  return false;
}

bool lldb_private::formatters::swift::ArraySyntheticFrontEnd::IsValid() {
  if (m_array_buffer)
    return m_array_buffer->IsValid();
  return false;
}

bool lldb_private::formatters::swift::ArraySyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::swift::ArraySyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (!m_array_buffer)
    return UINT32_MAX;
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildren())
    return UINT32_MAX;
  return idx;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::swift::ArraySyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;

  ArraySyntheticFrontEnd *front_end = new ArraySyntheticFrontEnd(valobj_sp);
  if (front_end && front_end->IsValid())
    return front_end;
  return nullptr;
}

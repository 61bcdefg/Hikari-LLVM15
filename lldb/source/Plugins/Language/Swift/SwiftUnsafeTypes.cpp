#include "SwiftUnsafeTypes.h"
#include "SwiftBasicTypes.h"

#include "Plugins/LanguageRuntime/Swift/SwiftLanguageRuntime.h"
#include "Plugins/TypeSystem/Swift/SwiftASTContext.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include <utility>

using namespace lldb;
using namespace lldb_private;

namespace {

class SwiftUnsafeType {
public:
  enum class UnsafePointerKind {
    eSwiftUnsafePointer,
    eSwiftUnsafeRawPointer,
    eSwiftUnsafeBufferPointer,
    eSwiftUnsafeRawBufferPointer,
  };

  static std::unique_ptr<SwiftUnsafeType> Create(ValueObject &valobj);
  size_t GetCount() const { return m_count; }
  addr_t GetStartAddress() const { return m_start_addr; }
  CompilerType GetElementType() const { return m_elem_type; }
  UnsafePointerKind GetKind() const { return m_kind; }
  bool HasPointee() const {
    return m_count == 1 && m_kind == UnsafePointerKind::eSwiftUnsafePointer;
  }
  virtual bool Update() = 0;

  virtual ~SwiftUnsafeType() = default;

protected:
  SwiftUnsafeType(ValueObject &valobj, UnsafePointerKind kind);
  addr_t GetAddress(llvm::StringRef child_name);

  ValueObject &m_valobj;
  const UnsafePointerKind m_kind;
  size_t m_count;
  addr_t m_start_addr;
  CompilerType m_elem_type;
};

SwiftUnsafeType::SwiftUnsafeType(ValueObject &valobj, UnsafePointerKind kind)
    : m_valobj(*valobj.GetNonSyntheticValue().get()), m_kind(kind) {}

lldb::addr_t SwiftUnsafeType::GetAddress(llvm::StringRef child_name) {
  ConstString name(child_name);
  ValueObjectSP optional_value_sp(m_valobj.GetChildMemberWithName(name, true));
  if (!optional_value_sp || !optional_value_sp->GetNumChildren()) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't unwrap the 'Swift.Optional' ValueObject child "
             "named {1}.",
             __FUNCTION__, name);
    return false;
  }

  ValueObjectSP unsafe_ptr_value_sp =
      optional_value_sp->GetChildAtIndex(0, true);
  if (!unsafe_ptr_value_sp || !unsafe_ptr_value_sp->GetNumChildren()) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't unwrap the 'Swift.UnsafePointer' ValueObject child "
             "named 'some'.",
             __FUNCTION__);
    return false;
  }

  CompilerType type = unsafe_ptr_value_sp->GetCompilerType();
  if (!type.IsValid()) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't get the compiler type for the "
             "'Swift.UnsafePointer' ValueObject.",
             __FUNCTION__, type.GetTypeName());
    return false;
  }

  auto *type_system =
      llvm::dyn_cast_or_null<TypeSystemSwift>(type.GetTypeSystem());
  if (!type_system) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't get {1} type system.", __FUNCTION__,
             type.GetTypeName());
    return false;
  }

  CompilerType argument_type =
      type_system->GetGenericArgumentType(type.GetOpaqueQualType(), 0);

  if (argument_type.IsValid())
    m_elem_type = argument_type;

  ValueObjectSP pointer_value_sp =
      unsafe_ptr_value_sp->GetChildAtIndex(0, true);
  if (!pointer_value_sp) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't unwrap the 'Swift.Int' ValueObject named "
             "'pointerValue'.",
             __FUNCTION__);
    return false;
  }

  return pointer_value_sp->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
}

class SwiftUnsafeBufferPointer final : public SwiftUnsafeType {
public:
  SwiftUnsafeBufferPointer(ValueObject &valobj);
  bool Update() override;
};

SwiftUnsafeBufferPointer::SwiftUnsafeBufferPointer(ValueObject &valobj)
    : SwiftUnsafeType(valobj, UnsafePointerKind::eSwiftUnsafeBufferPointer) {}

bool SwiftUnsafeBufferPointer::Update() {
  if (!m_valobj.GetNumChildren())
    return false;

  // Here is the layout of Swift's Unsafe[Mutable]BufferPointer.
  //
  //  ▿ UnsafeBufferPointer
  //    ▿ _position : Optional<UnsafePointer<Int>>
  //      ▿ some : UnsafePointer<Int>
  //        - pointerValue : Int
  //    - count : Int
  //
  // The structure has 2 children:
  //  1. The buffer `count` child stored as a Swift `Int` type. This entry is a
  // "value-providing synthetic children", so lldb need to access to its
  // children in order to get the  actual value.
  //  2. An Optional UnsafePointer to the buffer start address. To access the
  // pointer address, lldb unfolds every ValueObject child until reaching
  // `pointerValue`.

  static ConstString g_count("count");
  ValueObjectSP count_value_sp(m_valobj.GetChildMemberWithName(g_count, true));
  if (!count_value_sp) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't find ValueObject child member named '{1}'.",
             __FUNCTION__, g_count);
    return false;
  }

  ValueObjectSP value_provided_child_sp = nullptr;

  // Implement Swift's 'value-providing synthetic children' workaround.
  // Depending on whether the ValueObject type is a primitive or a structure,
  // lldb should prioritize the synthetic value children.
  // If it has no synthetic children then fallback to non synthetic children.
  ValueObjectSP synthetic = count_value_sp->GetSyntheticValue();
  if (synthetic)
    value_provided_child_sp = synthetic->GetChildAtIndex(0, true);
  if (!value_provided_child_sp)
    value_provided_child_sp = count_value_sp->GetChildAtIndex(0, true);
  // If neither child exists, fail.
  if (!value_provided_child_sp) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't extract 'value-providing synthetic children' from "
             "ValueObject 'count'.",
             __FUNCTION__);
    return false;
  }

  size_t count = value_provided_child_sp->GetValueAsUnsigned(UINT64_MAX);

  if (count == UINT64_MAX) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't get a valid value for ValueObject 'count'.",
             __FUNCTION__);
    return false;
  }

  m_count = count;

  addr_t start_addr = GetAddress("_position");

  if (!start_addr || start_addr == LLDB_INVALID_ADDRESS) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't get a valid address for ValueObject '_position'.",
             __FUNCTION__);
    return false;
  }

  m_start_addr = start_addr;

  return true;
}

class SwiftUnsafeRawBufferPointer final : public SwiftUnsafeType {
public:
  SwiftUnsafeRawBufferPointer(ValueObject &valobj);
  bool Update() override;

private:
  addr_t m_end_addr;
};

SwiftUnsafeRawBufferPointer::SwiftUnsafeRawBufferPointer(ValueObject &valobj)
    : SwiftUnsafeType(valobj, UnsafePointerKind::eSwiftUnsafeRawBufferPointer) {
}

bool SwiftUnsafeRawBufferPointer::Update() {
  if (!m_valobj.GetNumChildren())
    return false;

  // Here is the layout of Swift's UnsafeRaw[Mutable]BufferPointer.
  // It's a view of the raw bytes of the pointee object. Each byte is viewed as
  // a `UInt8` value independent of the type of values held in that memory.
  //
  // ▿ UnsafeRawBufferPointer
  //   ▿ _position : Optional<UnsafeRawPointer<Int>>
  //     ▿ some : UnsafeRawPointer<Int>
  //       - pointerValue : Int
  //   ▿ _end : Optional<UnsafeRawPointer<Int>>
  //     ▿ some : UnsafeRawPointer<Int>
  //       - pointerValue : Int
  //
  // The structure has 2 Optional UnsafePointers to the buffer's start address
  // and end address. To access the pointer address, lldb unfolds every
  // ValueObject child until reaching `pointerValue`.

  addr_t addr = GetAddress("_position");
  if (!addr || addr == LLDB_INVALID_ADDRESS) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't get a valid address for ValueObject '_position'.",
             __FUNCTION__);
    return false;
  }
  m_start_addr = addr;

  addr = GetAddress("_end");
  if (!addr || addr == LLDB_INVALID_ADDRESS) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't get a valid address for ValueObject '_end'.",
             __FUNCTION__);
    return false;
  }
  m_end_addr = addr;

  if (!m_elem_type.IsValid()) {
    CompilerType type = m_valobj.GetCompilerType();
    if (!type.IsValid()) {
      LLDB_LOG(GetLog(LLDBLog::DataFormatters),
               "{0}: Couldn't get a valid base compiler type.", __FUNCTION__);
      return false;
    }

    auto *type_system =
        llvm::dyn_cast_or_null<TypeSystemSwift>(type.GetTypeSystem());
    if (!type_system) {
      LLDB_LOG(GetLog(LLDBLog::DataFormatters),
               "{0}: Couldn't get {1} type system.", __FUNCTION__,
               type.GetTypeName());
      return false;
    }

    CompilerType compiler_type =
        type_system->GetTypeFromMangledTypename(ConstString("$ss5UInt8VD"));
    if (!compiler_type.IsValid()) {
      LLDB_LOG(GetLog(LLDBLog::DataFormatters),
               "{0}: Couldn't get a valid compiler type for 'Swift.UInt8'.",
               __FUNCTION__);
      return false;
    }

    m_elem_type = compiler_type;
  }

  auto opt_type_size = m_elem_type.GetByteSize(m_valobj.GetTargetSP().get());

  if (!opt_type_size) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't get element byte size.", __FUNCTION__);
    return false;
  }
  m_count = (m_end_addr - m_start_addr) / *opt_type_size;

  return true;
}

class SwiftUnsafePointer final : public SwiftUnsafeType {
public:
  SwiftUnsafePointer(ValueObject &valobj, UnsafePointerKind kind);
  bool Update() override;
};

SwiftUnsafePointer::SwiftUnsafePointer(ValueObject &valobj,
                                       UnsafePointerKind kind)
    : SwiftUnsafeType(valobj, kind) {}

bool SwiftUnsafePointer::Update() {
  if (!m_valobj.GetNumChildren())
    return false;

  // Here is the layout of Swift's Unsafe[Mutable]Pointer.
  //
  // ▿ Unsafe[Raw]Pointer
  //   ▿ _rawValue : Int
  //       - pointerValue : Int
  //

  CompilerType type = m_valobj.GetCompilerType();
  if (!type.IsValid()) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't get the compiler type for the "
             "'Swift.UnsafePointer' ValueObject.",
             __FUNCTION__, type.GetTypeName());
    return false;
  }

  auto *type_system =
      llvm::dyn_cast_or_null<TypeSystemSwift>(type.GetTypeSystem());
  if (!type_system) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't get {1} type system.", __FUNCTION__,
             type.GetTypeName());
    return false;
  }

  CompilerType argument_type =
      type_system->GetGenericArgumentType(type.GetOpaqueQualType(), 0);

  if (argument_type.IsValid())
    m_elem_type = argument_type;

  ValueObjectSP pointer_value_sp(m_valobj.GetChildAtIndex(0, true));
  if (!pointer_value_sp) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't unwrap the 'Swift.Int' ValueObject named "
             "'pointerValue'.",
             __FUNCTION__);
    return false;
  }

  addr_t addr = pointer_value_sp->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);

  if (!addr || addr == LLDB_INVALID_ADDRESS)
    return false;

  m_start_addr = addr;
  m_count = (m_elem_type.IsValid()) ? 1 : 0;

  return true;
}

std::unique_ptr<SwiftUnsafeType> SwiftUnsafeType::Create(ValueObject &valobj) {
  CompilerType type = valobj.GetCompilerType();
  if (!type.IsValid()) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't get a valid base compiler type.", __FUNCTION__);
    return nullptr;
  }

  // Resolve the base Typedefed CompilerType.
  while (true) {
    opaque_compiler_type_t qual_type = type.GetOpaqueQualType();
    if (!qual_type) {
      LLDB_LOG(GetLog(LLDBLog::DataFormatters),
               "{0}: Couldn't get {1} opaque compiler type.", __FUNCTION__,
               type.GetTypeName());
      return nullptr;
    }

    auto *type_system =
        llvm::dyn_cast_or_null<TypeSystemSwift>(type.GetTypeSystem());
    if (!type_system) {
      LLDB_LOG(GetLog(LLDBLog::DataFormatters),
               "{0}: Couldn't get {1} type system.", __FUNCTION__,
               type.GetTypeName());
      return nullptr;
    }

    if (!type_system->IsTypedefType(qual_type))
      break;

    type = type_system->GetTypedefedType(qual_type);
  }

  if (!type.IsValid()) {
    LLDB_LOG(GetLog(LLDBLog::DataFormatters),
             "{0}: Couldn't resolve a valid base compiler type.", __FUNCTION__);
    return nullptr;
  }

  llvm::StringRef valobj_type_name(type.GetTypeName().GetCString());
  bool is_raw = valobj_type_name.contains("Raw");
  bool is_buffer_ptr = valobj_type_name.contains("BufferPointer");
  UnsafePointerKind kind =
      static_cast<UnsafePointerKind>(is_buffer_ptr << 1 | is_raw);

  switch (kind) {
  case UnsafePointerKind::eSwiftUnsafePointer:
  case UnsafePointerKind::eSwiftUnsafeRawPointer:
    return std::make_unique<SwiftUnsafePointer>(valobj, kind);
  case UnsafePointerKind::eSwiftUnsafeBufferPointer:
    return std::make_unique<SwiftUnsafeBufferPointer>(valobj);
  case UnsafePointerKind::eSwiftUnsafeRawBufferPointer:
    return std::make_unique<SwiftUnsafeRawBufferPointer>(valobj);
  }
}

} // namespace

bool lldb_private::formatters::swift::UnsafeTypeSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  std::unique_ptr<SwiftUnsafeType> unsafe_ptr = SwiftUnsafeType::Create(valobj);

  if (!unsafe_ptr || !unsafe_ptr->Update())
    return false;
  size_t count = unsafe_ptr->GetCount();
  addr_t addr = unsafe_ptr->GetStartAddress();

  auto is_buffer_ptr = [](SwiftUnsafeType::UnsafePointerKind kind) {
    return static_cast<int>(kind) & (1 << 1);
  };

  // Hide the number of children if not BufferPointer type.
  if (!is_buffer_ptr(unsafe_ptr->GetKind()))
    stream.Printf("0x%" PRIx64, addr);
  else
    stream.Printf("%zu %s (0x%" PRIx64 ")", count,
                  (count == 1) ? "value" : "values", addr);

  return true;
}

namespace lldb_private {
namespace formatters {
namespace swift {
class UnsafeTypeSyntheticFrontEnd : public SwiftBasicTypeSyntheticFrontEnd {
public:
  UnsafeTypeSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  size_t CalculateNumChildren() override;
  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;
  bool Update() override;
  bool MightHaveChildren() override;
  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  ExecutionContextRef m_exe_ctx_ref;
  uint8_t m_ptr_size;
  lldb::ByteOrder m_order;

  std::unique_ptr<SwiftUnsafeType> m_unsafe_ptr;
  size_t m_element_stride;
  WritableDataBufferSP m_buffer_sp;
  std::vector<ValueObjectSP> m_children;
};
} // namespace swift
} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::swift::UnsafeTypeSyntheticFrontEnd::
    UnsafeTypeSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SwiftBasicTypeSyntheticFrontEnd(*valobj_sp.get()) {

  ProcessSP process_sp = valobj_sp->GetProcessSP();
  if (!process_sp)
    return;

  m_ptr_size = process_sp->GetAddressByteSize();
  m_order = process_sp->GetByteOrder();

  m_unsafe_ptr = ::SwiftUnsafeType::Create(*valobj_sp.get());

  lldb_assert(m_unsafe_ptr != nullptr, "Could not create Swift Unsafe Type",
              __FUNCTION__, __FILE__, __LINE__);

  if (valobj_sp)
    Update();
}

size_t lldb_private::formatters::swift::UnsafeTypeSyntheticFrontEnd::
    CalculateNumChildren() {
  return m_unsafe_ptr->GetCount();
}

lldb::ValueObjectSP
lldb_private::formatters::swift::UnsafeTypeSyntheticFrontEnd::GetChildAtIndex(
    size_t idx) {
  const size_t num_children = CalculateNumChildren();

  if (idx >= num_children || idx >= m_children.size())
    return lldb::ValueObjectSP();

  return m_children[idx];
}

bool lldb_private::formatters::swift::UnsafeTypeSyntheticFrontEnd::Update() {
  m_children.clear();
  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return false;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();

  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return false;
  if (!m_unsafe_ptr->Update())
    return false;

  const addr_t start_addr = m_unsafe_ptr->GetStartAddress();
  const size_t num_children = CalculateNumChildren();
  const CompilerType element_type = m_unsafe_ptr->GetElementType();

  auto stride = element_type.GetByteStride(process_sp.get());
  if (!stride)
    return false;

  m_element_stride = *stride;

  if (m_children.empty()) {
    size_t buffer_size = num_children * m_element_stride;
    m_buffer_sp.reset(new DataBufferHeap(buffer_size, 0));

    Status error;
    size_t read_bytes = process_sp->ReadMemory(
        start_addr, m_buffer_sp->GetBytes(), buffer_size, error);

    if (!read_bytes || error.Fail())
      return false;

    DataExtractor buffer_data(m_buffer_sp->GetBytes(),
                              m_buffer_sp->GetByteSize(), m_order, m_ptr_size);

    // UnsafePointer/UnsafeMutablePointer have a `pointee` property.
    if (m_unsafe_ptr->HasPointee()) {
      DataExtractor data(buffer_data, 0, m_element_stride);
      m_children.push_back(CreateValueObjectFromData(
          "pointee", data, m_exe_ctx_ref, element_type));
      return true;
    }

    for (size_t i = 0; i < num_children; i++) {
      StreamString idx_name;
      idx_name.Printf("[%zu]", i);
      DataExtractor data(buffer_data, i * m_element_stride, m_element_stride);
      m_children.push_back(CreateValueObjectFromData(
          idx_name.GetString(), data, m_exe_ctx_ref, element_type));
    }
  }

  return m_children.size() == num_children;
}

bool lldb_private::formatters::swift::UnsafeTypeSyntheticFrontEnd::
    MightHaveChildren() {
  return m_unsafe_ptr->GetCount();
}

size_t lldb_private::formatters::swift::UnsafeTypeSyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (m_unsafe_ptr->HasPointee() && name == "pointee")
    return 0;
  return UINT32_MAX;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::swift::UnsafeTypeSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  return (new UnsafeTypeSyntheticFrontEnd(valobj_sp));
}

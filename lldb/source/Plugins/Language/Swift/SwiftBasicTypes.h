//===-- SwiftBasicTypes.h ---------------------------------------*- C++ -*-===//
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

#ifndef liblldb_SwiftBasicTypes_h_
#define liblldb_SwiftBasicTypes_h_

#include "lldb/lldb-forward.h"

#include "lldb/DataFormatters/TypeSynthetic.h"

namespace lldb_private {
namespace formatters {
namespace swift {

class SwiftBasicTypeSyntheticFrontEnd : public SyntheticValueProviderFrontEnd {
public:
  SwiftBasicTypeSyntheticFrontEnd(ValueObject &backend)
      : SyntheticValueProviderFrontEnd(backend) {}

  lldb::ValueObjectSP GetSyntheticValue() override;
};

SyntheticChildrenFrontEnd *
SwiftBasicTypeSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                       lldb::ValueObjectSP);
}
}
}

#endif // liblldb_SwiftDictionary_h_

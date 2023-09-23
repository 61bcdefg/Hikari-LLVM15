//===-- SWIG Interface for SBScriptObject -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

namespace lldb {

class SBScriptObject
{
public:
  SBScriptObject(const ScriptObjectPtr ptr, lldb::ScriptLanguage lang);

  SBScriptObject(const lldb::SBScriptObject &rhs);

  ~SBScriptObject();

  const lldb::SBScriptObject &operator=(const lldb::SBScriptObject &rhs);

  explicit operator bool() const;

  bool operator!=(const SBScriptObject &rhs) const;

  bool IsValid() const;

  lldb::ScriptObjectPtr GetPointer() const;

  lldb::ScriptLanguage GetLanguage() const;

#ifdef SWIGPYTHON
    // operator== is a free function, which swig does not handle, so we inject
    // our own equality operator here
    %pythoncode%{
    def __eq__(self, other):
      return not self.__ne__(other)
    %}
#endif


#ifdef SWIGPYTHON
    %pythoncode %{
        ptr = property(GetPointer, None, doc='''A read only property that returns the underlying script object.''')
        lang = property(GetLanguage, None, doc='''A read only property that returns the script language associated with with this script object.''')
    %}
#endif

};

}

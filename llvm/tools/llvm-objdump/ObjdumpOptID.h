#ifndef LLVM_TOOLS_LLVM_OBJDUMP_OBJDUMP_OPT_ID_H
#define LLVM_TOOLS_LLVM_OBJDUMP_OBJDUMP_OPT_ID_H

#include "llvm/Option/OptTable.h"

enum ObjdumpOptID {
  OBJDUMP_INVALID = 0, // This is not an option ID.
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(OBJDUMP_, PREFIX, NAME, ID, KIND, GROUP,     \
                                  ALIAS, ALIASARGS, FLAGS, PARAM, HELPTEXT,    \
                                  METAVAR, VALUES),
#include "ObjdumpOpts.inc"
#undef OPTION
};

#endif // LLVM_TOOLS_LLVM_OBJDUMP_OBJDUMP_OPT_ID_H

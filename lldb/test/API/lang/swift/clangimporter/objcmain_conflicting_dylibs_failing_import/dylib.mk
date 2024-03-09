DYLIB_ONLY := YES
DYLIB_NAME := $(BASENAME)
DYLIB_SWIFT_SOURCES := $(DYLIB_NAME).swift
SWIFTFLAGS_EXTRAS = \
  -Xcc -I$(SRCDIR) -Xcc -I$(SRCDIR)/$(BASENAME) \
  -emit-objc-header-path $(BASENAME).h

SWIFT_OBJC_INTEROP := 1

include Makefile.rules

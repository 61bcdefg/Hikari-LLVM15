# TestSwiftUnknownSelf.py
#
# This source file is part of the Swift.org open source project
#
# Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See https://swift.org/LICENSE.txt for license information
# See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
#
# ------------------------------------------------------------------------------
import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil
import os
import unittest2


class TestSwiftUnknownSelf(lldbtest.TestBase):

    mydir = lldbtest.TestBase.compute_mydir(__file__)

    def check_class(self, var_self, broken):
        lldbutil.check_variable(self, var_self, num_children=2)
        m_base_string = var_self.GetChildMemberWithName("base_string")
        m_string = var_self.GetChildMemberWithName("string")
        if not broken:
            lldbutil.check_variable(self, m_base_string, summary='"hello"')
        lldbutil.check_variable(self, m_string, summary='"world"')
        # Also check the expression evaluator.
        self.expect("expr self", substrs=["hello", "world"])
        self.expect("fr v self", substrs=["hello", "world"])


    @skipIf(bugnumber="SR-10216", archs=['ppc64le'])
    @swiftTest
    @skipUnlessFoundation
    def test_unknown_self_objc_ref(self):
        """Test unknown references to Objective-C objects."""
        self.build()
        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, 'break here', lldb.SBFileSpec('main.swift'))

        for i in range(2):
            self.assertTrue(thread.GetStopReason() == lldb.eStopReasonBreakpoint)
            frame = thread.frames[0]
            var_self = frame.FindVariable("self")
            self.check_class(var_self, broken=False)
            process.Continue()

            # weak
            self.assertTrue(thread.GetStopReason() == lldb.eStopReasonBreakpoint)
            frame = thread.frames[0]
            var_self = frame.FindVariable("self")
            self.check_class(var_self, broken=True)
            process.Continue()

        self.assertTrue(thread.GetStopReason() != lldb.eStopReasonBreakpoint)



"""
Test the formatting of briged Swift metatypes
"""
import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbutil as lldbutil
import os
import unittest2


class TestSwiftBridgedMetatype(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def setUp(self):
        TestBase.setUp(self)

    @swiftTest
    @skipUnlessFoundation
    def test_swift_bridged_metatype(self):
        """Test the formatting of bridged Swift metatypes"""
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, 'Set breakpoint here', lldb.SBFileSpec('main.swift'))

        var_k = self.frame().FindVariable("k")
        if sys.platform.startswith("linux"):
            lldbutil.check_variable(self, var_k, False, "Foundation.NSString")
        else:
            lldbutil.check_variable(self, var_k, False, "NSString")

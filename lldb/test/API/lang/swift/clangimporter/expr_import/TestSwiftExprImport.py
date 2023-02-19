import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbutil as lldbutil
import unittest2

class TestSwiftExprImport(TestBase):

    mydir = TestBase.compute_mydir(__file__)
    
    def setUp(self):
        TestBase.setUp(self)

    # Don't run ClangImporter tests if Clangimporter is disabled.
    @skipIf(setting=('symbols.use-swift-clangimporter', 'false'))
    @swiftTest
    def test(self):
        """Test error handling if the expression evaluator
           encounters a Clang import failure.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(self, "break here",
                                          lldb.SBFileSpec('main.swift'))
        self.expect("expr -- import A", error=True,
                    substrs=['SYNTAX_ERROR',
                             'could', 'not', 'build', 'module'])

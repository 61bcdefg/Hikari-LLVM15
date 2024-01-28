import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftWerror(TestBase):
    mydir = TestBase.compute_mydir(__file__)
    NO_DEBUG_INFO_TESTCASE = True

    def setUp(self):
        TestBase.setUp(self)

    # Don't run ClangImporter tests if Clangimporter is disabled.
    @skipIf(setting=("symbols.use-swift-clangimporter", "false"))
    @skipUnlessDarwin
    @swiftTest
    def test(self):
        """This tests that -Werror is removed from ClangImporter options by
        introducing two conflicting macro definitions in different dylibs.
        """
        self.build()
        target, _, _, _ = lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("dylib.swift"), extra_images=["Dylib"]
        )

        # Turn on logging.
        log = self.getBuildArtifact("types.log")
        self.expect('log enable lldb types -f "%s"' % log)

        self.expect("expression foo", DATA_TYPES_DISPLAYED_CORRECTLY, substrs=["42"])
        self.filecheck('platform shell cat "%s"' % log, __file__)
#       CHECK-NOT: SwiftASTContextForExpressions{{.*}}-Werror
#       CHECK:     SwiftASTContextForExpressions{{.*}}-DCONFLICT
#       CHECK-NOT: SwiftASTContextForExpressions{{.*}}-Werror
#       CHECK:     SwiftASTContextForExpressions{{.*}}-DCONFLICT
#       CHECK-NOT: SwiftASTContextForExpressions{{.*}}-DCONFLICT
#       CHECK-NOT: SwiftASTContextForExpressions{{.*}}-Werror

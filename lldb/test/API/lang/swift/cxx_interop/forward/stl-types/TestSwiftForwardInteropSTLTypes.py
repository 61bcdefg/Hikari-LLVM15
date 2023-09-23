"""
Test that a C++ class is visible in Swift.
"""
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *


class TestSwiftForwardInteropSTLTypes(TestBase):
    @skipIfLinux  # rdar://106532498
    @skipIf(
        setting=("symbols.use-swift-clangimporter", "false")
    )  # rdar://106438227 (TestSTLTypes fails when clang importer is disabled)
    @swiftTest
    def test(self):
        self.build()

        _, _, _, _ = lldbutil.run_to_source_breakpoint(
            self, "Set breakpoint here", lldb.SBFileSpec("main.swift")
        )

        self.expect(
            "v map",
            substrs=[
                "CxxMap",
                "first = 1, second = 3",
                "first = 2, second = 2",
                "first = 3, second = 3",
            ],
        )
        self.expect(
            "expr map",
            substrs=[
                "CxxMap",
                "first = 1, second = 3",
                "first = 2, second = 2",
                "first = 3, second = 3",
            ],
        )

        self.expect(
            "v optional",
            substrs=[
                "CxxOptional",
                "optional",
                "Has Value=true",
                'Value = "In optional!"',
            ],
        )
        self.expect(
            "expr optional",
            substrs=["CxxOptional", "Has Value=true", 'Value = "In optional!"'],
        )

        self.expect(
            "v emptyOptional",
            substrs=["CxxOptional", "emptyOptional", "Has Value=false"],
        )
        self.expect("expr emptyOptional", substrs=["CxxOptional", "Has Value=false"])

        self.expect("v set", substrs=["CxxSet", "size=3", "3.7", "4.2", "9.19"])
        self.expect("expr set", substrs=["CxxSet", "size=3", "3.7", "4.2", "9.19"])

        self.expect("v string", substrs=["string", "Hello from C++!"])
        self.expect("expr string", substrs=["Hello from C++!"])

        self.expect(
            "v unorderedMap",
            substrs=[
                "CxxUnorderedMap",
                '(first = 3, second = "three")',
                '(first = 2, second = "two")',
                '(first = 1, second = "one")',
            ],
            ordered=False,
        )
        self.expect(
            "expr unorderedMap",
            substrs=[
                "CxxUnorderedMap",
                '(first = 3, second = "three")',
                '(first = 2, second = "two")',
                '(first = 1, second = "one")',
            ],
            ordered=False,
        )

        self.expect(
            "v unorderedSet",
            substrs=["CxxUnorderedSet", "first", "second", "third"],
            ordered=False,
        )
        self.expect(
            "expr unorderedSet",
            substrs=["CxxUnorderedSet", "first", "second", "third"],
            ordered=False,
        )

        self.expect(
            "v vector", substrs=["CxxVector", "[0] = 4.1", "[1] = 3.7", "[2] = 9.19"]
        )
        self.expect(
            "expr vector", substrs=["CxxVector", "[0] = 4.1", "[1] = 3.7", "[2] = 9.19"]
        )

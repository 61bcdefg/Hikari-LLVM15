"""
Test that variables passed in as a class constrained protocol type
are correctly printed.
"""
import lldbsuite.test.lldbinline as lldbinline
from lldbsuite.test.decorators import *

lldbinline.MakeInlineTest(
    __file__, globals(),
            decorators=[
            swiftTest,skipUnlessDarwin])

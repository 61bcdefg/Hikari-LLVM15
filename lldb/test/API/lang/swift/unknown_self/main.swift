// main.swift
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
// -----------------------------------------------------------------------------
import Foundation

// Auxiliary functions.
func use(_ c : ObjCClass) {}
func use(_ c : PureClass) {}
func doCall(_ fn : () -> ()) { fn() }

// Pure Swift objects.
class PureBase {
  let base_string = "hello"
}

class PureClass : PureBase {
  let string = "world"

  func f() {
    doCall() { [unowned self] in
      use(self) // break here
    }
    doCall() { [weak self] in
      use(self!) // break here
    }
  }
}

// Objective-C objects.
class Base : NSObject {
  let base_string = "hello"
}

class ObjCClass : Base {
  let string = "world"

  func f() {
    doCall() { [unowned self] in
      use(self) // break here
    }
    doCall() { [weak self] in
      use(self!) // break here
    }
  }
}

PureClass().f()
ObjCClass().f()

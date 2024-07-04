// main.swift
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
// -----------------------------------------------------------------------------

func h<T>(_ t: T) -> T {
    return t // break here
}

func g<U, T>(_ pair : (T, U)) -> T {
  return h(pair.0) // other breakpoint
}

func f(_ arg1: Int, _ arg2: String) -> Int {
    return g((arg1, arg2))
}

f(12, "Hello world")

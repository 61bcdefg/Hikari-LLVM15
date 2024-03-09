protocol P {
  func foo() -> Int32
}

public class C: P {
  var x: Int32 = 11223344
  public func foo() -> Int32  {
    return x
  }
}

public struct S : P {
  var x: Int32 = 44332211
  public func foo() -> Int32  {
    return x
  }
}

func foo<T1: P, T2: P> (_ t1: T1, _ t2: T2) -> Int32 {
  return t1.foo() + t2.foo() //% self.expect('frame variable -d run -- t1', substrs=['11223344'])
   //% self.expect('frame variable -d run -- t2', substrs=['44332211'])
   //% self.expect('expression -d run -- t1', substrs=['11223344'])
   //% self.expect('expression -d run -- t2', substrs=['44332211'])
}

print(foo(C(), S()))

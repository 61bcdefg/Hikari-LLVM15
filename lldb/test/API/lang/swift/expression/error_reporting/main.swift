class State {
  init(x: Int) {
    number = x
    print("in class") // break here
  }

  var number : Int
}

struct S {}

func f(_ strct : S) {
  print("in function") // break here
}

f(S())
State(x: 20)

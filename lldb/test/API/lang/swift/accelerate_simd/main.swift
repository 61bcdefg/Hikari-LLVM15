func main() -> Int {
  let d4 = SIMD4<Double>(1.5, 2, 3, 4)
  let patatino = SIMD4<Int>(1,2,3,4)
  let tinky = SIMD2<Int>(12, 24)
  print((d4, patatino, tinky)) //%self.expect('frame variable d4', substrs=['1.5', '2', '3', '4'])
                               //%self.expect('frame var patatino', substrs=['(1, 2, 3, 4)'])
                               //%self.expect('frame var tinky', substrs=['(12, 24)'])
  return 0
}

main()

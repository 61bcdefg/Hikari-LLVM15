// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
#include "llvm/Transforms/Obfuscation/CryptoUtils.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

using namespace llvm;
namespace llvm {
ManagedStatic<CryptoUtils> cryptoutils;
}
CryptoUtils::CryptoUtils() {}

uint32_t
CryptoUtils::scramble32(uint32_t in,
                        std::map<uint32_t /*IDX*/, uint32_t /*VAL*/> &VMap) {
  if (VMap.find(in) == VMap.end()) {
    uint32_t V = get_uint32_t();
    VMap[in] = V;
    return V;
  } else {
    return VMap[in];
  }
}
CryptoUtils::~CryptoUtils() {
  if (eng != nullptr)
    delete eng;
}
void CryptoUtils::prng_seed() {
  using namespace std::chrono;
  std::uint_fast64_t ms =
      duration_cast<milliseconds>(system_clock::now().time_since_epoch())
          .count();
  errs() << format("std::mt19937_64 seeded with current timestamp: %" PRIu64 "",
                   ms)
         << "\n";
  eng = new std::mt19937_64(ms);
}
void CryptoUtils::prng_seed(std::uint_fast64_t seed) {
  errs() << format("std::mt19937_64 seeded with: %" PRIu64 "", seed) << "\n";
  eng = new std::mt19937_64(seed);
}
std::uint_fast64_t CryptoUtils::get_raw() {
  if (eng == nullptr)
    prng_seed();
  return (*eng)();
}
uint32_t CryptoUtils::get_range(uint32_t min, uint32_t max) {
  if (max == 0)
    return 0;
  std::uniform_int_distribution<uint32_t> dis(min, max - 1);
  return dis(*eng);
}

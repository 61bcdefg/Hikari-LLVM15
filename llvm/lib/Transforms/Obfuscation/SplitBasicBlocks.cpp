// For open-source license, please refer to [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
#include "llvm/Transforms/Obfuscation/CryptoUtils.h"
#include "llvm/Transforms/Obfuscation/Split.h"
#include "llvm/Transforms/Obfuscation/Utils.h"

#define DEBUG_TYPE "split"

using namespace llvm;
using namespace std;

// Stats
STATISTIC(Split, "Basicblock splitted");

static cl::opt<int> SplitNum("split_num", cl::init(2),
                             cl::desc("Split <split_num> time each BB"));

namespace {
struct SplitBasicBlock : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  bool flag;
  SplitBasicBlock() : FunctionPass(ID) { this->flag = true; }
  SplitBasicBlock(bool flag) : FunctionPass(ID) { this->flag = flag; }

  bool runOnFunction(Function &F) override;
  void split(Function *f);

  bool containsPHI(BasicBlock *b);
  bool containsSwiftError(BasicBlock *b);
  void shuffle(std::vector<int> &vec);
};
} // namespace

char SplitBasicBlock::ID = 0;
INITIALIZE_PASS(SplitBasicBlock, "splitobf", "Enable BasicBlockSpliting.", true,
                true)
FunctionPass *llvm::createSplitBasicBlockPass(bool flag) {
  return new SplitBasicBlock(flag);
}

bool SplitBasicBlock::runOnFunction(Function &F) {
  // Check if the number of applications is correct
  if (!((SplitNum > 1) && (SplitNum <= 10))) {
    errs() << "Split application basic block percentage\
            -split_num=x must be 1 < x <= 10";
    return false;
  }

  Function *tmp = &F;
  // Do we obfuscate
  if (toObfuscate(flag, tmp, "split")) {
    errs() << "Running BasicBlockSplit On " << tmp->getName() << "\n";
    split(tmp);
    ++Split;
  }

  return true;
}

void SplitBasicBlock::split(Function *f) {
  std::vector<BasicBlock *> origBB;
  int splitN = SplitNum;

  // Save all basic blocks
  for (BasicBlock &BB : *f)
    origBB.emplace_back(&BB);

  for (BasicBlock *curr : origBB) {
    // No need to split a 1 inst bb
    // Or ones containing a PHI node
    // Isn't compatible with SwiftError
    if (curr->size() < 2 || containsPHI(curr) ||
        containsSwiftError(curr))
      continue;

    // Check splitN and current BB size
    if ((size_t)splitN > curr->size())
      splitN = curr->size() - 1;

    // Generate splits point
    std::vector<int> test;
    for (unsigned i = 1; i < curr->size(); ++i)
      test.emplace_back(i);

    // Shuffle
    if (test.size() != 1) {
      shuffle(test);
      std::sort(test.begin(), test.begin() + splitN);
    }

    // Split
    BasicBlock::iterator it = curr->begin();
    BasicBlock *toSplit = curr;
    int last = 0;
    for (int i = 0; i < splitN; ++i) {
      for (int j = 0; j < test[i] - last; ++j)
        ++it;
      last = test[i];
      if (toSplit->size() < 2)
        continue;

      // https://github.com/eshard/obfuscator-llvm/commit/fff24c7a1555aa3ae7160056b06ba1e0b3d109db
      /* TODO: find a real fix or try with the probe-stack inline-asm when its
       * ready. See https://github.com/Rust-for-Linux/linux/issues/355.
       * Sometimes moving an alloca from the entry block to the second block
       * causes a segfault when using the "probe-stack" attribute (observed with
       * with Rust programs). To avoid this issue we just split the entry block
       * after the allocas in this case.
       */
      if (f->hasFnAttribute("probe-stack") && curr->isEntryBlock()
          && isa<AllocaInst>(it))
        continue;

      toSplit = toSplit->splitBasicBlock(it, toSplit->getName() + ".split");
    }

    ++Split;
  }
}

bool SplitBasicBlock::containsPHI(BasicBlock *b) {
  for (Instruction &I : *b)
    if (isa<PHINode>(&I))
      return true;
  return false;
}

bool SplitBasicBlock::containsSwiftError(BasicBlock *b) {
  for (Instruction &I : *b)
    if (AllocaInst *AI = dyn_cast<AllocaInst>(&I))
      if (AI->isSwiftError())
        return true;
  return false;
}

void SplitBasicBlock::shuffle(std::vector<int> &vec) {
  int n = vec.size();
  for (int i = n - 1; i > 0; --i)
    std::swap(vec[i], vec[cryptoutils->get_uint32_t() % (i + 1)]);
}

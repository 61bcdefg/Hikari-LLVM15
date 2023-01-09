// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
#include "llvm/Transforms/Obfuscation/CryptoUtils.h"
#include "llvm/Transforms/Obfuscation/Split.h"
#include "llvm/Transforms/Obfuscation/Utils.h"

#define DEBUG_TYPE "split"

using namespace llvm;
using namespace std;

static cl::opt<size_t>
    s_user_split_num("split_num", cl::init(2),
                     cl::desc("Split <split_num> time each BB"));

namespace {
struct SplitBasicBlock : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  bool flag;
  SplitBasicBlock() : FunctionPass(ID) { this->flag = true; }
  SplitBasicBlock(bool flag) : FunctionPass(ID) { this->flag = flag; }

  bool runOnFunction(Function &F) override;
  void split(Function *F);

  bool containsPHI(BasicBlock *BB);
  bool containsSwiftError(BasicBlock *BB);
  void shuffle(std::vector<size_t> &vec);
};
} // namespace

bool SplitBasicBlock::runOnFunction(Function &F) {
  // Check if the number of applications is correct
  if (!((s_user_split_num > 1) && (s_user_split_num <= 10))) {
    errs() << "Split application basic block percentage -split_num=x must be 1 "
              "< x <= 10";
    return false;
  }

  Function *tmp = &F;
  // Do we obfuscate
  if (toObfuscate(flag, tmp, "split")) {
    errs() << "Running BasicBlockSplit On " << tmp->getName() << "\n";
    split(tmp);
  }

  return true;
}

void SplitBasicBlock::split(Function *F) {
  // errs() << "\n>>>>>>>>>>> SplitBasicBlock::split() entry <<<<<<<<<<<<<<<\n";
  // errs() << ">>>>> Running on the function: " << F->getName() << "\n";
  std::vector<BasicBlock *> origBB;
  size_t split_ctr = 0;
  size_t bb_number = 0;

  // Save all basic blocks
  for (BasicBlock &BB : *F) {
    origBB.emplace_back(&BB);
  }

  // errs() << ">>>>> Number of BB in the function: " << origBB.size() << "\n";

  for (BasicBlock *currBB : origBB) {
    // errs() <<
    // "\n================================================================\n";
    // errs() << ">>>>> Iterating over the BB #" << bb_number << "\n";
    bb_number++;

    // No need to split a 1 inst bb
    // Or ones containing a PHI node
    // Isn't compatible with SwiftError
    if (currBB->size() < 2 || containsPHI(currBB) ||
        containsSwiftError(currBB)) {
      // errs() << ">>>>> Skipping the small BB\n";
      continue;
    }

    // Check split_ctr and current BB size (the number of splits must not exceed
    // the number of LLVM inst in the BB minus one)
    if (s_user_split_num > currBB->size() - 1) {
      // errs() << ">>>>> Setting new split_ctr value to fit the BB being
      // processed: " << currBB->size() - 1 << "\n";
      split_ctr = currBB->size() - 1;
    } else {
      split_ctr = s_user_split_num;
    }

    // Generate splits point (count number of the LLVM instructions in the
    // current BB)
    std::vector<size_t> llvm_inst_ord;
    for (size_t i = 1; i < currBB->size(); ++i) {
      llvm_inst_ord.emplace_back(i);
    }
    // errs() << ">>>>> Number of instructions in the BB being processed: " <<
    // llvm_inst_ord.size() << "\n";

    // Shuffle
    shuffle(llvm_inst_ord);
    std::sort(llvm_inst_ord.begin(), llvm_inst_ord.begin() + split_ctr);

    // Split
    size_t llvm_inst_prev_offset = 0;
    BasicBlock::iterator curr_bb_it = currBB->begin();
    BasicBlock *curr_bb_offset = currBB;

    // errs() << ">>>>> Actual split_ctr before splitting: " << split_ctr <<
    // "\n";

    for (size_t i = 0; i < split_ctr; ++i) {
      // errs() << ">>>>> i: " << i << ", llvm_inst_ord[i]: " <<
      // llvm_inst_ord[i] << ", llvm_inst_prev_offset: " <<
      // llvm_inst_prev_offset << "\n";

      for (size_t j = 0; j < llvm_inst_ord[i] - llvm_inst_prev_offset; ++j) {
        ++curr_bb_it;
      }

      llvm_inst_prev_offset = llvm_inst_ord[i];

      // https://github.com/eshard/obfuscator-llvm/commit/fff24c7a1555aa3ae7160056b06ba1e0b3d109db
      /* TODO: find a real fix or try with the probe-stack inline-asm when its
       * ready. See https://github.com/Rust-for-Linux/linux/issues/355.
       * Sometimes moving an alloca from the entry block to the second block
       * causes a segfault when using the "probe-stack" attribute (observed with
       * with Rust programs). To avoid this issue we just split the entry block
       * after the allocas in this case.
       */
      if (F->hasFnAttribute("probe-stack") && currBB->isEntryBlock() &&
          isa<AllocaInst>(curr_bb_it)) {
        continue;
      }

      curr_bb_offset = curr_bb_offset->splitBasicBlock(
          curr_bb_it, curr_bb_offset->getName() + ".split");
    }
  }
}

bool SplitBasicBlock::containsPHI(BasicBlock *BB) {
  for (Instruction &I : *BB) {
    if (isa<PHINode>(&I)) {
      return true;
    }
  }

  return false;
}

bool SplitBasicBlock::containsSwiftError(BasicBlock *BB) {
  for (Instruction &I : *BB)
    if (AllocaInst *AI = dyn_cast<AllocaInst>(&I))
      if (AI->isSwiftError())
        return true;
  return false;
}

void SplitBasicBlock::shuffle(std::vector<size_t> &vec) {
  int n = vec.size();
  for (int i = n - 1; i > 0; --i)
    std::swap(vec[i], vec[cryptoutils->get_range(i + 1)]);
}

char SplitBasicBlock::ID = 0;
INITIALIZE_PASS(SplitBasicBlock, "splitobf", "Enable BasicBlockSpliting.", true,
                true)

FunctionPass *llvm::createSplitBasicBlockPass(bool flag) {
  return new SplitBasicBlock(flag);
}

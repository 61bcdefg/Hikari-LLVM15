// For open-source license, please refer to
// [License](https://github.com/HikariObfuscator/Hikari/wiki/License).
//===----------------------------------------------------------------------===//
//===- BogusControlFlow.cpp - BogusControlFlow Obfuscation
// pass-------------------------===//
//
// This file implements BogusControlFlow's pass, inserting bogus control flow.
// It adds bogus flow to a given basic block this way:
//
// Before :
// 	         		     entry
//      			       |
//  	    	  	 ______v______
//   	    		|   Original  |
//   	    		|_____________|
//             		       |
// 		        	       v
//		        	     return
//
// After :
//           		     entry
//             		       |
//            		   ____v_____
//      			  |condition*| (false)
//           		  |__________|----+
//           		 (true)|          |
//             		       |          |
//           		 ______v______    |
// 		        +-->|   Original* |   |
// 		        |   |_____________| (true)
// 		        |   (false)|    !-----------> return
// 		        |    ______v______    |
// 		        |   |   Altered   |<--!
// 		        |   |_____________|
// 		        |__________|
//
//  * The results of these terminator's branch's conditions are always true, but
//  these predicates are
//    opacificated. For this, we declare two global values: x and y, and replace
//    the FCMP_TRUE predicate with (y < 10 || x * (x + 1) % 2 == 0) (this could
//    be improved, as the global values give a hint on where are the opaque
//    predicates)
//
//  The altered bloc is a copy of the original's one with junk instructions
//  added accordingly to the type of instructions we found in the bloc
//
//  Each basic block of the function is choosen if a random number in the range
//  [0,100] is smaller than the choosen probability rate. The default value
//  is 30. This value can be modify using the option -boguscf-prob=[value].
//  Value must be an integer in the range [0, 100], otherwise the default value
//  is taken. Exemple: -boguscf -boguscf-prob=60
//
//  The pass can also be loop many times on a function, including on the basic
//  blocks added in a previous loop. Be careful if you use a big probability
//  number and choose to run the loop many times wich may cause the pass to run
//  for a very long time. The default value is one loop, but you can change it
//  with -boguscf-loop=[value]. Value must be an integer greater than 1,
//  otherwise the default value is taken. Exemple: -boguscf -boguscf-loop=2
//
//
//  Defined debug types:
//  - "gen" : general informations
//  - "opt" : concerning the given options (parameter)
//  - "cfg" : printing the various function's cfg before transformation
//	      and after transformation if it has been modified, and all
//	      the functions at end of the pass, after doFinalization.
//
//  To use them all, simply use the -debug option.
//  To use only one of them, follow the pass' command by -debug-only=name.
//  Exemple, -boguscf -debug-only=cfg
//
//
//  Stats:
//  The following statistics will be printed if you use
//  the -stats command:
//
// a. Number of functions in this module
// b. Number of times we run on each function
// c. Initial number of basic blocks in this module
// d. Number of modified basic blocks
// e. Number of added basic blocks in this module
// f. Final number of basic blocks in this module
//
// file   : lib/Transforms/Obfuscation/BogusControlFlow.cpp
// date   : june 2012
// version: 1.0
// author : julie.michielin@gmail.com
// modifications: pjunod, Rinaldini Julien
// project: Obfuscator
// option : -boguscf
//
//===----------------------------------------------------------------------------------===//

#include "llvm/Transforms/Obfuscation/BogusControlFlow.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/Obfuscation/Utils.h"
#include "llvm/Transforms/Utils/Local.h"
#include <memory>

// Options for the pass
const int defaultObfRate = 70, defaultObfTime = 1;

static cl::opt<int>
    ObfProbRate("bcf_prob",
                cl::desc("Choose the probability [%] each basic blocks will be "
                         "obfuscated by the -bcf pass"),
                cl::value_desc("probability rate"), cl::init(defaultObfRate),
                cl::Optional);

static cl::opt<int>
    ObfTimes("bcf_loop",
             cl::desc("Choose how many time the -bcf pass loop on a function"),
             cl::value_desc("number of times"), cl::init(defaultObfTime),
             cl::Optional);
static cl::opt<int> ConditionExpressionComplexity(
    "bcf_cond_compl",
    cl::desc("The complexity of the expression used to generate branching "
             "condition"),
    cl::value_desc("Complexity"), cl::init(3), cl::Optional);

static cl::opt<bool> JunkAssembly(
    "bcf_junkasm",
    cl::desc("Whether to add junk assembly to altered basic block"),
    cl::value_desc("add junk assembly"), cl::init(false), cl::Optional);
static cl::opt<int> MaxNumberOfJunkAssembly(
    "bcf_junkasm_maxnum",
    cl::desc("The maximum number of junk assembliy per altered basic block"),
    cl::value_desc("max number of junk assembly"), cl::init(3), cl::Optional);
static cl::opt<int> MinNumberOfJunkAssembly(
    "bcf_junkasm_minnum",
    cl::desc("The minimum number of junk assembliy per altered basic block"),
    cl::value_desc("min number of junk assembly"), cl::init(1), cl::Optional);
static cl::opt<bool> CreateFunctionForOpaquePredicate(
    "bcf_createfunc", cl::desc("Create function for each opaque predicate"),
    cl::value_desc("create function"), cl::init(false), cl::Optional);

static Instruction::BinaryOps ops[] = {
    Instruction::Add, Instruction::Sub, Instruction::And, Instruction::Or,
    Instruction::Xor, Instruction::Mul, Instruction::UDiv};
static CmpInst::Predicate preds[] = {CmpInst::ICMP_EQ,  CmpInst::ICMP_NE,
                                     CmpInst::ICMP_UGT, CmpInst::ICMP_UGE,
                                     CmpInst::ICMP_ULT, CmpInst::ICMP_ULE};
namespace {
static bool OnlyUsedBy(Value *V, Value *Usr) {
  for (User *U : V->users())
    if (U != Usr)
      return false;
  return true;
}
static void RemoveDeadConstant(Constant *C) {
  assert(C->use_empty() && "Constant is not dead!");
  SmallPtrSet<Constant *, 4> Operands;
  for (Value *Op : C->operands())
    if (OnlyUsedBy(Op, C))
      Operands.insert(cast<Constant>(Op));
  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(C)) {
    if (!GV->hasLocalLinkage())
      return; // Don't delete non-static globals.
    GV->eraseFromParent();
  } else if (!isa<Function>(C))
    if (isa<ArrayType>(C->getType()) || isa<StructType>(C->getType()) ||
        isa<VectorType>(C->getType()))
      C->destroyConstant();

  // If the constant referenced anything, see if we can delete it as well.
  for (Constant *O : Operands)
    RemoveDeadConstant(O);
}
struct BogusControlFlow : public FunctionPass {
  static char ID; // Pass identification
  bool flag;
  vector<ICmpInst *> needtoedit;
  BogusControlFlow() : FunctionPass(ID) { this->flag = true; }
  BogusControlFlow(bool flag) : FunctionPass(ID) { this->flag = flag; }
  /* runOnFunction
   *
   * Overwrite FunctionPass method to apply the transformation
   * to the function. See header for more details.
   */
  bool runOnFunction(Function &F) override {
    // Check if the percentage is correct
    if (ObfTimes <= 0) {
      errs() << "BogusControlFlow application number -bcf_loop=x must be x > 0";
      return false;
    }

    // Check if the number of applications is correct
    if (!((ObfProbRate > 0) && (ObfProbRate <= 100))) {
      errs() << "BogusControlFlow application basic blocks percentage "
                "-bcf_prob=x must be 0 < x <= 100";
      return false;
    }

    // Check if the number of applications is correct
    if (MaxNumberOfJunkAssembly < MinNumberOfJunkAssembly) {
      errs() << "BogusControlFlow application numbers of junk asm "
                "-bcf_junkasm_maxnum=x must be x >= bcf_junkasm_minnum";
      return false;
    }

    // If fla annotations
    if (toObfuscate(flag, &F, "bcf")) {
      if (F.isPresplitCoroutine())
        return false;
      if (F.getName().startswith("HikariBCFOpaquePredicateFunction"))
        return false;
      errs() << "Running BogusControlFlow On " << F.getName() << "\n";
      bogus(F);
      doF(F);
    }

    return true;
  } // end of runOnFunction()

  void bogus(Function &F) {
    int NumObfTimes = ObfTimes;

    // Real begining of the pass
    // Loop for the number of time we run the pass on the function
    do {
      // Put all the function's block in a list
      std::list<BasicBlock *> basicBlocks;
      for (BasicBlock &BB : F)
        if (!BB.isEHPad() && !BB.isLandingPad() && !containsSwiftError(&BB))
          basicBlocks.emplace_back(&BB);

      while (!basicBlocks.empty()) {
        // Basic Blocks' selection
        if ((int)llvm::cryptoutils->get_range(100) <= ObfProbRate) {
          // Add bogus flow to the given Basic Block (see description)
          BasicBlock *basicBlock = basicBlocks.front();
          addBogusFlow(basicBlock, F);
        }
        // remove the block from the list
        basicBlocks.pop_front();
      } // end of while(!basicBlocks.empty())
    } while (--NumObfTimes > 0);
  }

  bool containsSwiftError(BasicBlock *b) {
    for (Instruction &I : *b)
      if (AllocaInst *AI = dyn_cast<AllocaInst>(&I))
        if (AI->isSwiftError())
          return true;
    return false;
  }

  /* addBogusFlow
   *
   * Add bogus flow to a given basic block, according to the header's
   * description
   */
  void addBogusFlow(BasicBlock *basicBlock, Function &F) {

    // Split the block: first part with only the phi nodes and debug info and
    // terminator
    //                  created by splitBasicBlock. (-> No instruction)
    //                  Second part with every instructions from the original
    //                  block
    // We do this way, so we don't have to adjust all the phi nodes, metadatas
    // and so on for the first block. We have to let the phi nodes in the first
    // part, because they actually are updated in the second part according to
    // them.
    BasicBlock::iterator i1 = basicBlock->begin();
    if (basicBlock->getFirstNonPHIOrDbgOrLifetime())
      i1 = (BasicBlock::iterator)basicBlock->getFirstNonPHIOrDbgOrLifetime();

    // https://github.com/eshard/obfuscator-llvm/commit/85c8719c86bcb4784f5a436e28f3496e91cd6292
    /* TODO: find a real fix or try with the probe-stack inline-asm when its
     * ready. See https://github.com/Rust-for-Linux/linux/issues/355. Sometimes
     * moving an alloca from the entry block to the second block causes a
     * segfault when using the "probe-stack" attribute (observed with with Rust
     * programs). To avoid this issue we just split the entry block after the
     * allocas in this case.
     */
    if (F.hasFnAttribute("probe-stack") && basicBlock->isEntryBlock()) {
      // Find the first non alloca instruction
      while ((i1 != basicBlock->end()) && isa<AllocaInst>(i1))
        i1++;

      // If there are no other kind of instruction we just don't split that
      // entry block
      if (i1 == basicBlock->end())
        return;
    }

    BasicBlock *originalBB = basicBlock->splitBasicBlock(i1, "originalBB");

    // Creating the altered basic block on which the first basicBlock will jump
    BasicBlock *alteredBB =
        createAlteredBasicBlock(originalBB, "alteredBB", &F);

    // Now that all the blocks are created,
    // we modify the terminators to adjust the control flow.

    alteredBB->getTerminator()->eraseFromParent();
    basicBlock->getTerminator()->eraseFromParent();

    // Preparing a condition..
    // For now, the condition is an always true comparaison between 2 float
    // This will be complicated after the pass (in doFinalization())

    // We need to use ConstantInt instead of ConstantFP as ConstantFP results in
    // strange dead-loop when injected into Xcode
    Value *LHS = ConstantInt::get(Type::getInt32Ty(F.getContext()), 1);
    Value *RHS = ConstantInt::get(Type::getInt32Ty(F.getContext()), 1);

    // The always true condition. End of the first block
    ICmpInst *condition = new ICmpInst(*basicBlock, ICmpInst::ICMP_EQ, LHS, RHS,
                                       "BCFPlaceHolderPred");
    needtoedit.emplace_back(condition);

    // Jump to the original basic block if the condition is true or
    // to the altered block if false.
    BranchInst::Create(originalBB, alteredBB, condition, basicBlock);

    // The altered block loop back on the original one.
    BranchInst::Create(originalBB, alteredBB);

    // The end of the originalBB is modified to give the impression that
    // sometimes it continues in the loop, and sometimes it return the desired
    // value (of course it's always true, so it always use the original
    // terminator..
    //  but this will be obfuscated too;) )

    // iterate on instruction just before the terminator of the originalBB
    BasicBlock::iterator i = originalBB->end();

    // Split at this point (we only want the terminator in the second part)
    BasicBlock *originalBBpart2 =
        originalBB->splitBasicBlock(--i, "originalBBpart2");
    // the first part go either on the return statement or on the begining
    // of the altered block.. So we erase the terminator created when splitting.
    originalBB->getTerminator()->eraseFromParent();
    // We add at the end a new always true condition
    ICmpInst *condition2 = new ICmpInst(*originalBB, CmpInst::ICMP_EQ, LHS, RHS,
                                        "BCFPlaceHolderPred");
    needtoedit.emplace_back(condition2);
    // Do random behavior to avoid pattern recognition.
    // This is achieved by jumping to a random BB
    switch (llvm::cryptoutils->get_range(2)) {
    case 0: {
      BranchInst::Create(originalBBpart2, originalBB, condition2, originalBB);
      break;
    }
    case 1: {
      BranchInst::Create(originalBBpart2, alteredBB, condition2, originalBB);
      break;
    }
    default: {
      BranchInst::Create(originalBBpart2, originalBB, condition2, originalBB);
      break;
    }
    }
  } // end of addBogusFlow()

  /* createAlteredBasicBlock
   *
   * This function return a basic block similar to a given one.
   * It's inserted just after the given basic block.
   * The instructions are similar but junk instructions are added between
   * the cloned one. The cloned instructions' phi nodes, metadatas, uses and
   * debug locations are adjusted to fit in the cloned basic block and
   * behave nicely.
   */
  BasicBlock *createAlteredBasicBlock(BasicBlock *basicBlock,
                                      const Twine &Name = "gen",
                                      Function *F = nullptr) {
    // Useful to remap the informations concerning instructions.
    ValueToValueMapTy VMap;
    BasicBlock *alteredBB = CloneBasicBlock(basicBlock, VMap, Name, F);
    // Remap operands.
    BasicBlock::iterator ji = basicBlock->begin();
    for (BasicBlock::iterator i = alteredBB->begin(), e = alteredBB->end();
         i != e; ++i) {
      // Loop over the operands of the instruction
      for (User::op_iterator opi = i->op_begin(), ope = i->op_end(); opi != ope;
           ++opi) {
        // get the value for the operand
        Value *v = MapValue(
            *opi, VMap, RF_NoModuleLevelChanges,
            0); // https://github.com/eshard/obfuscator-llvm/commit/e8ba79332bd63a3eb38eb85a636951f1cb1f22df
        if (v != 0)
          *opi = v;
      }
      // Remap phi nodes' incoming blocks.
      if (PHINode *pn = dyn_cast<PHINode>(i)) {
        for (unsigned j = 0, e = pn->getNumIncomingValues(); j != e; ++j) {
          Value *v = MapValue(pn->getIncomingBlock(j), VMap, RF_None, 0);
          if (v != 0)
            pn->setIncomingBlock(j, cast<BasicBlock>(v));
        }
      }
      // Remap attached metadata.
      SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
      i->getAllMetadata(MDs);
      // important for compiling with DWARF, using option -g.
      i->setDebugLoc(ji->getDebugLoc());
      ji++;
    } // The instructions' informations are now all correct

    // add random instruction in the middle of the bloc. This part can be
    // improve
    for (BasicBlock::iterator i = alteredBB->begin(), e = alteredBB->end();
         i != e; ++i) {
      // in the case we find binary operator, we modify slightly this part by
      // randomly insert some instructions
      if (i->isBinaryOp()) { // binary instructions
        unsigned int opcode = i->getOpcode();
        Instruction *op, *op1 = nullptr;
        Twine *var = new Twine("_");
        // treat differently float or int
        // Binary int
        if (opcode == Instruction::Add || opcode == Instruction::Sub ||
            opcode == Instruction::Mul || opcode == Instruction::UDiv ||
            opcode == Instruction::SDiv || opcode == Instruction::URem ||
            opcode == Instruction::SRem || opcode == Instruction::Shl ||
            opcode == Instruction::LShr || opcode == Instruction::AShr ||
            opcode == Instruction::And || opcode == Instruction::Or ||
            opcode == Instruction::Xor) {
          for (int random = (int)llvm::cryptoutils->get_range(10); random < 10;
               ++random) {
            switch (llvm::cryptoutils->get_range(4)) { // to improve
            case 0:                                    // do nothing
              break;
            case 1:
              op = BinaryOperator::CreateNeg(i->getOperand(0), *var, &*i);
              op1 = BinaryOperator::Create(Instruction::Add, op,
                                           i->getOperand(1), "gen", &*i);
              break;
            case 2:
              op1 = BinaryOperator::Create(Instruction::Sub, i->getOperand(0),
                                           i->getOperand(1), *var, &*i);
              op = BinaryOperator::Create(Instruction::Mul, op1,
                                          i->getOperand(1), "gen", &*i);
              break;
            case 3:
              op = BinaryOperator::Create(Instruction::Shl, i->getOperand(0),
                                          i->getOperand(1), *var, &*i);
              break;
            }
          }
        }
        // Binary float
        if (opcode == Instruction::FAdd || opcode == Instruction::FSub ||
            opcode == Instruction::FMul || opcode == Instruction::FDiv ||
            opcode == Instruction::FRem) {
          for (int random = (int)llvm::cryptoutils->get_range(10); random < 10;
               ++random) {
            switch (llvm::cryptoutils->get_range(3)) { // can be improved
            case 0:                                    // do nothing
              break;
            case 1:
              op = UnaryOperator::CreateFNeg(i->getOperand(0), *var, &*i);
              op1 = BinaryOperator::Create(Instruction::FAdd, op,
                                           i->getOperand(1), "gen", &*i);
              break;
            case 2:
              op = BinaryOperator::Create(Instruction::FSub, i->getOperand(0),
                                          i->getOperand(1), *var, &*i);
              op1 = BinaryOperator::Create(Instruction::FMul, op,
                                           i->getOperand(1), "gen", &*i);
              break;
            }
          }
        }
        if (opcode == Instruction::ICmp) { // Condition (with int)
          ICmpInst *currentI = (ICmpInst *)(&i);
          switch (llvm::cryptoutils->get_range(3)) { // must be improved
          case 0:                                    // do nothing
            break;
          case 1:
            currentI->swapOperands();
            break;
          case 2: // randomly change the predicate
            switch (llvm::cryptoutils->get_range(10)) {
            case 0:
              currentI->setPredicate(ICmpInst::ICMP_EQ);
              break; // equal
            case 1:
              currentI->setPredicate(ICmpInst::ICMP_NE);
              break; // not equal
            case 2:
              currentI->setPredicate(ICmpInst::ICMP_UGT);
              break; // unsigned greater than
            case 3:
              currentI->setPredicate(ICmpInst::ICMP_UGE);
              break; // unsigned greater or equal
            case 4:
              currentI->setPredicate(ICmpInst::ICMP_ULT);
              break; // unsigned less than
            case 5:
              currentI->setPredicate(ICmpInst::ICMP_ULE);
              break; // unsigned less or equal
            case 6:
              currentI->setPredicate(ICmpInst::ICMP_SGT);
              break; // signed greater than
            case 7:
              currentI->setPredicate(ICmpInst::ICMP_SGE);
              break; // signed greater or equal
            case 8:
              currentI->setPredicate(ICmpInst::ICMP_SLT);
              break; // signed less than
            case 9:
              currentI->setPredicate(ICmpInst::ICMP_SLE);
              break; // signed less or equal
            }
            break;
          }
        }
        if (opcode == Instruction::FCmp) { // Conditions (with float)
          FCmpInst *currentI = (FCmpInst *)(&i);
          switch (llvm::cryptoutils->get_range(3)) { // must be improved
          case 0:                                    // do nothing
            break;
          case 1:
            currentI->swapOperands();
            break;
          case 2: // randomly change the predicate
            switch (llvm::cryptoutils->get_range(10)) {
            case 0:
              currentI->setPredicate(FCmpInst::FCMP_OEQ);
              break; // ordered and equal
            case 1:
              currentI->setPredicate(FCmpInst::FCMP_ONE);
              break; // ordered and operands are unequal
            case 2:
              currentI->setPredicate(FCmpInst::FCMP_UGT);
              break; // unordered or greater than
            case 3:
              currentI->setPredicate(FCmpInst::FCMP_UGE);
              break; // unordered, or greater than, or equal
            case 4:
              currentI->setPredicate(FCmpInst::FCMP_ULT);
              break; // unordered or less than
            case 5:
              currentI->setPredicate(FCmpInst::FCMP_ULE);
              break; // unordered, or less than, or equal
            case 6:
              currentI->setPredicate(FCmpInst::FCMP_OGT);
              break; // ordered and greater than
            case 7:
              currentI->setPredicate(FCmpInst::FCMP_OGE);
              break; // ordered and greater than or equal
            case 8:
              currentI->setPredicate(FCmpInst::FCMP_OLT);
              break; // ordered and less than
            case 9:
              currentI->setPredicate(FCmpInst::FCMP_OLE);
              break; // ordered or less than, or equal
            }
            break;
          }
        }
      }
    }
    // Remove DIs from AlterBB
    vector<CallInst *> toRemove;
    vector<Constant *> DeadConstants;
    for (Instruction &I : *alteredBB) {
      if (CallInst *CI = dyn_cast<CallInst>(&I)) {
        if (CI->getCalledFunction() != nullptr &&
            CI->getCalledFunction()->getName().startswith("llvm.dbg"))
          toRemove.emplace_back(CI);
      }
    }
    // Shamefully stolen from IPO/StripSymbols.cpp
    for (CallInst *CI : toRemove) {
      Value *Arg1 = CI->getArgOperand(0);
      Value *Arg2 = CI->getArgOperand(1);
      assert(CI->use_empty() && "llvm.dbg intrinsic should have void result");
      CI->eraseFromParent();
      if (Arg1->use_empty()) {
        if (Constant *C = dyn_cast<Constant>(Arg1))
          DeadConstants.emplace_back(C);
        else
          RecursivelyDeleteTriviallyDeadInstructions(Arg1);
      }
      if (Arg2->use_empty())
        if (Constant *C = dyn_cast<Constant>(Arg2))
          DeadConstants.emplace_back(C);
    }
    while (!DeadConstants.empty()) {
      Constant *C = DeadConstants.back();
      DeadConstants.pop_back();
      if (GlobalVariable *GV = dyn_cast<GlobalVariable>(C)) {
        if (GV->hasLocalLinkage())
          RemoveDeadConstant(GV);
      } else {
        RemoveDeadConstant(C);
      }
    }
    if (JunkAssembly) {
      string junk = "";
      for (uint32_t i = cryptoutils->get_range(MinNumberOfJunkAssembly,
                                               MaxNumberOfJunkAssembly);
           i > 0; i--)
        junk += ".long " + to_string(cryptoutils->get_uint32_t()) + "\n";
      InlineAsm *IA = InlineAsm::get(
          FunctionType::get(Type::getVoidTy(alteredBB->getContext()), false),
          junk, "", true, false);
      CallInst::Create(IA, None, "", &*alteredBB->getFirstInsertionPt());
    }
    return alteredBB;
  } // end of createAlteredBasicBlock()

  /* doF
   *
   * This part obfuscate the always true predicates generated in addBogusFlow()
   * of the function.
   */
  bool doF(Function &F) {
    vector<Instruction *> toEdit, toDelete;
    // Looking for the conditions and branches to transform
    for (BasicBlock &BB : F) {
      Instruction *tbb = BB.getTerminator();
      if (BranchInst *br = dyn_cast<BranchInst>(tbb)) {
        if (br->isConditional()) {
          ICmpInst *cond = dyn_cast<ICmpInst>(br->getCondition());
          if (cond && std::find(needtoedit.begin(), needtoedit.end(), cond) !=
                          needtoedit.end()) {
            toDelete.emplace_back(cond); // The condition
            toEdit.emplace_back(tbb);    // The branch using the condition
          }
        }
      }
    }
    Module &M = *F.getParent();
    Type *I1Ty = Type::getInt1Ty(M.getContext());
    Type *I32Ty = Type::getInt32Ty(M.getContext());
    // Replacing all the branches we found
    for (Instruction *i : toEdit) {
      // Previously We Use LLVM EE To Calculate LHS and RHS
      // Since IRBuilder<> uses ConstantFolding to fold constants.
      // The return instruction is already returning constants
      // The variable names below are the artifact from the Emulation Era
      Function *emuFunction = Function::Create(
          FunctionType::get(I32Ty, false),
          GlobalValue::LinkageTypes::PrivateLinkage, "HikariBCFEmuFunction", M);
      BasicBlock *emuEntryBlock =
          BasicBlock::Create(emuFunction->getContext(), "", emuFunction);

      Function *opFunction = nullptr;
      IRBuilder<> *IRBOp = nullptr;
      if (CreateFunctionForOpaquePredicate) {
        opFunction = Function::Create(FunctionType::get(I1Ty, false),
                                      GlobalValue::LinkageTypes::PrivateLinkage,
                                      "HikariBCFOpaquePredicateFunction", M);
        BasicBlock *opTrampBlock =
            BasicBlock::Create(opFunction->getContext(), "", opFunction);
        BasicBlock *opEntryBlock =
            BasicBlock::Create(opFunction->getContext(), "", opFunction);
        // Insert a br to make it can be obfuscated by IndirectBranch
        BranchInst::Create(opEntryBlock, opTrampBlock);
        IRBOp = new IRBuilder<>(opEntryBlock);
      }
      Instruction *tmp = &*(i->getParent()->getFirstNonPHIOrDbgOrLifetime());
      IRBuilder<> *IRBReal = new IRBuilder<>(tmp);
      IRBuilder<> IRBEmu(emuEntryBlock);
      // First,Construct a real RHS that will be used in the actual condition
      Constant *RealRHS = ConstantInt::get(I32Ty, cryptoutils->get_uint32_t());
      // Prepare Initial LHS and RHS to bootstrap the emulator
      Constant *LHSC =
          ConstantInt::get(I32Ty, cryptoutils->get_range(1, UINT32_MAX));
      Constant *RHSC =
          ConstantInt::get(I32Ty, cryptoutils->get_range(1, UINT32_MAX));
      GlobalVariable *LHSGV =
          new GlobalVariable(M, Type::getInt32Ty(M.getContext()), false,
                             GlobalValue::PrivateLinkage, LHSC, "LHSGV");
      GlobalVariable *RHSGV =
          new GlobalVariable(M, Type::getInt32Ty(M.getContext()), false,
                             GlobalValue::PrivateLinkage, RHSC, "RHSGV");
      LoadInst *LHS =
          (CreateFunctionForOpaquePredicate ? IRBOp : IRBReal)
              ->CreateLoad(LHSGV->getValueType(), LHSGV, "Initial LHS");
      LoadInst *RHS =
          (CreateFunctionForOpaquePredicate ? IRBOp : IRBReal)
              ->CreateLoad(RHSGV->getValueType(), RHSGV, "Initial LHS");

      // To Speed-Up Evaluation
      Value *emuLHS = LHSC;
      Value *emuRHS = RHSC;
      Instruction::BinaryOps initialOp =
          ops[llvm::cryptoutils->get_range(sizeof(ops) / sizeof(ops[0]))];
      Value *emuLast =
          IRBEmu.CreateBinOp(initialOp, emuLHS, emuRHS, "EmuInitialCondition");
      Value *Last = (CreateFunctionForOpaquePredicate ? IRBOp : IRBReal)
                        ->CreateBinOp(initialOp, LHS, RHS, "InitialCondition");
      for (int i = 0; i < ConditionExpressionComplexity; i++) {
        Constant *newTmp =
            ConstantInt::get(I32Ty, cryptoutils->get_range(1, UINT32_MAX));
        Instruction::BinaryOps initialOp2 =
            ops[llvm::cryptoutils->get_range(sizeof(ops) / sizeof(ops[0]))];
        emuLast = IRBEmu.CreateBinOp(initialOp2, emuLast, newTmp,
                                     "EmuInitialCondition");
        Last = (CreateFunctionForOpaquePredicate ? IRBOp : IRBReal)
                   ->CreateBinOp(initialOp2, Last, newTmp, "InitialCondition");
      }
      // Randomly Generate Predicate
      CmpInst::Predicate pred =
          preds[llvm::cryptoutils->get_range(sizeof(preds) / sizeof(preds[0]))];
      if (CreateFunctionForOpaquePredicate) {
        IRBOp->CreateRet(IRBOp->CreateICmp(pred, Last, RealRHS));
        Last = IRBReal->CreateCall(opFunction);
      } else
        Last = IRBReal->CreateICmp(pred, Last, RealRHS);
      emuLast = IRBEmu.CreateICmp(pred, emuLast, RealRHS);
      ReturnInst *RI = IRBEmu.CreateRet(emuLast);
      ConstantInt *emuCI = cast<ConstantInt>(RI->getReturnValue());
      APInt emulateResult = emuCI->getValue();
      if (emulateResult == 1) {
        // Our ConstantExpr evaluates to true;
        BranchInst::Create(((BranchInst *)i)->getSuccessor(0),
                           ((BranchInst *)i)->getSuccessor(1), Last,
                           i->getParent());
      } else {
        // False, swap operands
        BranchInst::Create(((BranchInst *)i)->getSuccessor(1),
                           ((BranchInst *)i)->getSuccessor(0), Last,
                           i->getParent());
      }
      emuFunction->eraseFromParent();
      i->eraseFromParent(); // erase the branch
    }
    // Erase all the associated conditions we found
    for (Instruction *i : toDelete)
      i->eraseFromParent();
    return true;
  } // end of doFinalization
};  // end of struct BogusControlFlow : public FunctionPass
} // namespace

char BogusControlFlow::ID = 0;
INITIALIZE_PASS(BogusControlFlow, "bcfobf", "Enable BogusControlFlow.", true,
                true)
FunctionPass *llvm::createBogusControlFlowPass(bool flag) {
  return new BogusControlFlow(flag);
}

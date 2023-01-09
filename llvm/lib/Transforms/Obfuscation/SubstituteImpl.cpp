//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Obfuscation/SubstituteImpl.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/NoFolder.h"

#define NUMBER_ADD_SUBST 7
#define NUMBER_SUB_SUBST 6
#define NUMBER_AND_SUBST 6
#define NUMBER_OR_SUBST 6
#define NUMBER_XOR_SUBST 6
#define NUMBER_MUL_SUBST 2

static void addNeg(BinaryOperator *bo);
static void addDoubleNeg(BinaryOperator *bo);
static void addRand(BinaryOperator *bo);
static void addRand2(BinaryOperator *bo);
static void addSubstitution(BinaryOperator *bo);
static void addSubstitution2(BinaryOperator *bo);
static void addSubstitution3(BinaryOperator *bo);

static void subNeg(BinaryOperator *bo);
static void subRand(BinaryOperator *bo);
static void subRand2(BinaryOperator *bo);
static void subSubstitution(BinaryOperator *bo);
static void subSubstitution2(BinaryOperator *bo);
static void subSubstitution3(BinaryOperator *bo);

static void andSubstitution(BinaryOperator *bo);
static void andSubstitution2(BinaryOperator *bo);
static void andSubstitution3(BinaryOperator *bo);
static void andSubstitutionRand(BinaryOperator *bo);
static void andNor(BinaryOperator *bo);
static void andNand(BinaryOperator *bo);

static void orSubstitution(BinaryOperator *bo);
static void orSubstitution2(BinaryOperator *bo);
static void orSubstitution3(BinaryOperator *bo);
static void orSubstitutionRand(BinaryOperator *bo);
static void orNor(BinaryOperator *bo);
static void orNand(BinaryOperator *bo);

static void xorSubstitution(BinaryOperator *bo);
static void xorSubstitution2(BinaryOperator *bo);
static void xorSubstitution3(BinaryOperator *bo);
static void xorSubstitutionRand(BinaryOperator *bo);
static void xorNor(BinaryOperator *bo);
static void xorNand(BinaryOperator *bo);

static void mulSubstitution(BinaryOperator *bo);
static void mulSubstitution2(BinaryOperator *bo);

static void (*funcAdd[NUMBER_ADD_SUBST])(BinaryOperator *bo) = {
    &addNeg,          &addDoubleNeg,     &addRand,         &addRand2,
    &addSubstitution, &addSubstitution2, &addSubstitution3};
static void (*funcSub[NUMBER_SUB_SUBST])(BinaryOperator *bo) = {
    &subNeg,          &subRand,          &subRand2,
    &subSubstitution, &subSubstitution2, &subSubstitution3};
static void (*funcAnd[NUMBER_AND_SUBST])(BinaryOperator *bo) = {
    &andSubstitution,     &andSubstitution2, &andSubstitution3,
    &andSubstitutionRand, &andNor,           &andNand};
static void (*funcOr[NUMBER_OR_SUBST])(BinaryOperator *bo) = {
    &orSubstitution,     &orSubstitution2, &orSubstitution3,
    &orSubstitutionRand, &orNor,           &orNand};
static void (*funcXor[NUMBER_XOR_SUBST])(BinaryOperator *bo) = {
    &xorSubstitution,     &xorSubstitution2, xorSubstitution3,
    &xorSubstitutionRand, &xorNor,           &xorNand};
static void (*funcMul[NUMBER_MUL_SUBST])(BinaryOperator *bo) = {
    &mulSubstitution, &mulSubstitution2};

void SubstituteImpl::substituteAdd(BinaryOperator *bo) {
  (*funcAdd[llvm::cryptoutils->get_range(NUMBER_ADD_SUBST)])(bo);
}
void SubstituteImpl::substituteSub(BinaryOperator *bo) {
  (*funcSub[llvm::cryptoutils->get_range(NUMBER_SUB_SUBST)])(bo);
}
void SubstituteImpl::substituteAnd(BinaryOperator *bo) {
  (*funcAnd[llvm::cryptoutils->get_range(NUMBER_AND_SUBST)])(bo);
}
void SubstituteImpl::substituteOr(BinaryOperator *bo) {
  (*funcOr[llvm::cryptoutils->get_range(NUMBER_OR_SUBST)])(bo);
}
void SubstituteImpl::substituteXor(BinaryOperator *bo) {
  (*funcXor[llvm::cryptoutils->get_range(NUMBER_XOR_SUBST)])(bo);
}
void SubstituteImpl::substituteMul(BinaryOperator *bo) {
  (*funcMul[llvm::cryptoutils->get_range(NUMBER_MUL_SUBST)])(bo);
}

// Implementation of ~(a | b) and ~a & ~b
static BinaryOperator *buildNor(Value *a, Value *b, Instruction *insertBefore) {
  switch (cryptoutils->get_range(2)) {
  case 0: {
    // ~(a | b)
    BinaryOperator *op =
        BinaryOperator::Create(Instruction::Or, a, b, "", insertBefore);
    op = BinaryOperator::CreateNot(op, "", insertBefore);
    return op;
  }
  case 1: {
    // ~a & ~b
    BinaryOperator *nota = BinaryOperator::CreateNot(a, "", insertBefore);
    BinaryOperator *notb = BinaryOperator::CreateNot(b, "", insertBefore);
    BinaryOperator *op =
        BinaryOperator::Create(Instruction::And, nota, notb, "", insertBefore);
    return op;
  }
  default:
    llvm_unreachable("wtf?");
  }
}

// Implementation of ~(a & b) and ~a | ~b
static BinaryOperator *buildNand(Value *a, Value *b,
                                 Instruction *insertBefore) {
  switch (cryptoutils->get_range(2)) {
  case 0: {
    // ~(a & b)
    BinaryOperator *op =
        BinaryOperator::Create(Instruction::And, a, b, "", insertBefore);
    op = BinaryOperator::CreateNot(op, "", insertBefore);
    return op;
  }
  case 1: {
    // ~a | ~b
    BinaryOperator *nota = BinaryOperator::CreateNot(a, "", insertBefore);
    BinaryOperator *notb = BinaryOperator::CreateNot(b, "", insertBefore);
    BinaryOperator *op =
        BinaryOperator::Create(Instruction::Or, nota, notb, "", insertBefore);
    return op;
  }
  default:
    llvm_unreachable("wtf?");
  }
}

// Implementation of a = b - (-c)
static void addNeg(BinaryOperator *bo) {
  BinaryOperator *op = BinaryOperator::CreateNeg(bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Sub, bo->getOperand(0), op, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = -(-b + (-c))
static void addDoubleNeg(BinaryOperator *bo) {
  BinaryOperator *op = BinaryOperator::CreateNeg(bo->getOperand(0), "", bo);
  BinaryOperator *op2 = BinaryOperator::CreateNeg(bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Add, op, op2, "", bo);
  op = BinaryOperator::CreateNeg(op, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of  r = rand (); a = b + r; a = a + c; a = a - r
static void addRand(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(
      bo->getType(), llvm::cryptoutils->get_uint64_t());
  BinaryOperator *op =
      BinaryOperator::Create(Instruction::Add, bo->getOperand(0), co, "", bo);
  op = BinaryOperator::Create(Instruction::Add, op, bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Sub, op, co, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of r = rand (); a = b - r; a = a + b; a = a + r
static void addRand2(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(
      bo->getType(), llvm::cryptoutils->get_uint64_t());
  BinaryOperator *op =
      BinaryOperator::Create(Instruction::Sub, bo->getOperand(0), co, "", bo);
  op = BinaryOperator::Create(Instruction::Add, op, bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Add, op, co, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b + c => a = b - ~c - 1
static void addSubstitution(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(bo->getType(), 1);
  BinaryOperator *op = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  BinaryOperator *op1 = BinaryOperator::CreateNeg(co, "", bo);
  op = BinaryOperator::Create(Instruction::Sub, op, op1, "", bo);
  op = BinaryOperator::Create(Instruction::Sub, bo->getOperand(0), op, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b + c => a = (b | c) + (b & c)
static void addSubstitution2(BinaryOperator *bo) {
  BinaryOperator *op = BinaryOperator::Create(
      Instruction::And, bo->getOperand(0), bo->getOperand(1), "", bo);
  BinaryOperator *op1 = BinaryOperator::Create(
      Instruction::Or, bo->getOperand(0), bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Add, op, op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b + c => a = (b ^ c) + (b & c) * 2
static void addSubstitution3(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(bo->getType(), 2);
  BinaryOperator *op = BinaryOperator::Create(
      Instruction::And, bo->getOperand(0), bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Mul, op, co, "", bo);
  BinaryOperator *op1 = BinaryOperator::Create(
      Instruction::Xor, bo->getOperand(0), bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Add, op1, op, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b + (-c)
static void subNeg(BinaryOperator *bo) {
  BinaryOperator *op = BinaryOperator::CreateNeg(bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Add, bo->getOperand(0), op, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of  r = rand (); a = b + r; a = a - c; a = a - r
static void subRand(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(
      bo->getType(), llvm::cryptoutils->get_uint64_t());
  BinaryOperator *op =
      BinaryOperator::Create(Instruction::Add, bo->getOperand(0), co, "", bo);
  op = BinaryOperator::Create(Instruction::Sub, op, bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Sub, op, co, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of  r = rand (); a = b - r; a = a - c; a = a + r
static void subRand2(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(
      bo->getType(), llvm::cryptoutils->get_uint64_t());
  BinaryOperator *op =
      BinaryOperator::Create(Instruction::Sub, bo->getOperand(0), co, "", bo);
  op = BinaryOperator::Create(Instruction::Sub, op, bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Add, op, co, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b - c => a = (b & ~c) - (~b & c)
static void subSubstitution(BinaryOperator *bo) {
  BinaryOperator *op1 = BinaryOperator::CreateNot(bo->getOperand(0), "", bo);
  BinaryOperator *op =
      BinaryOperator::Create(Instruction::And, op1, bo->getOperand(1), "", bo);
  op1 = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  BinaryOperator *op2 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(0), op1, "", bo);
  op = BinaryOperator::Create(Instruction::Sub, op2, op, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b - c => a = (2 * (b & ~c)) - (b ^ c)
static void subSubstitution2(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(bo->getType(), 2);
  BinaryOperator *op1 = BinaryOperator::Create(
      Instruction::Xor, bo->getOperand(0), bo->getOperand(1), "", bo);
  BinaryOperator *op = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::And, bo->getOperand(0), op, "", bo);
  op = BinaryOperator::Create(Instruction::Mul, co, op, "", bo);
  op = BinaryOperator::Create(Instruction::Sub, op, op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b - c => a = b + ~c + 1
static void subSubstitution3(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(bo->getType(), 1);
  BinaryOperator *op1 = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  BinaryOperator *op =
      BinaryOperator::Create(Instruction::Add, bo->getOperand(0), op1, "", bo);
  op = BinaryOperator::Create(Instruction::Add, op, co, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b & c => a = (b ^ ~c) & b
static void andSubstitution(BinaryOperator *bo) {
  BinaryOperator *op = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  BinaryOperator *op1 =
      BinaryOperator::Create(Instruction::Xor, bo->getOperand(0), op, "", bo);
  op = BinaryOperator::Create(Instruction::And, op1, bo->getOperand(0), "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b & c => a = (b | c) & ~(b ^ c)
static void andSubstitution2(BinaryOperator *bo) {
  BinaryOperator *op1 = BinaryOperator::Create(
      Instruction::Xor, bo->getOperand(0), bo->getOperand(1), "", bo);
  op1 = BinaryOperator::CreateNot(op1, "", bo);
  BinaryOperator *op = BinaryOperator::Create(
      Instruction::Or, bo->getOperand(0), bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::And, op, op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b & c => a = (~b | c) + (b + 1)
static void andSubstitution3(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(bo->getType(), 1);
  BinaryOperator *op1 =
      BinaryOperator::Create(Instruction::Add, bo->getOperand(0), co, "", bo);
  BinaryOperator *op = BinaryOperator::CreateNot(bo->getOperand(0), "", bo);
  op = BinaryOperator::Create(Instruction::Or, op, bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Add, op, op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a & b <=> ~(~a | ~b) & (r | ~r)
static void andSubstitutionRand(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(
      bo->getType(), llvm::cryptoutils->get_uint64_t());
  BinaryOperator *op = BinaryOperator::CreateNot(bo->getOperand(0), "", bo);
  BinaryOperator *op1 = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  BinaryOperator *opr = BinaryOperator::CreateNot(co, "", bo);
  BinaryOperator *opa =
      BinaryOperator::Create(Instruction::Or, op, op1, "", bo);
  opr = BinaryOperator::Create(Instruction::Or, co, opr, "", bo);
  op = BinaryOperator::CreateNot(opa, "", bo);
  op = BinaryOperator::Create(Instruction::And, op, opr, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a & b => Nor(Nor(a, a), Nor(b, b))
static void andNor(BinaryOperator *bo) {
  BinaryOperator *noraa = buildNor(bo->getOperand(0), bo->getOperand(0), bo);
  BinaryOperator *norbb = buildNor(bo->getOperand(1), bo->getOperand(1), bo);
  bo->replaceAllUsesWith(buildNor(noraa, norbb, bo));
}

// Implementation of a = a & b => Nand(Nand(a, b), Nand(a, b))
static void andNand(BinaryOperator *bo) {
  BinaryOperator *nandab = buildNand(bo->getOperand(0), bo->getOperand(1), bo);
  BinaryOperator *nandab2 = buildNand(bo->getOperand(0), bo->getOperand(1), bo);
  bo->replaceAllUsesWith(buildNand(nandab, nandab2, bo));
}

// Implementation of a = a | b => a = (b & c) | (b ^ c)
static void orSubstitution(BinaryOperator *bo) {
  BinaryOperator *op = BinaryOperator::Create(
      Instruction::And, bo->getOperand(0), bo->getOperand(1), "", bo);
  BinaryOperator *op1 = BinaryOperator::Create(
      Instruction::Xor, bo->getOperand(0), bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Or, op, op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a | b => a = (b + (b ^ c)) - (b & ~c)
static void orSubstitution2(BinaryOperator *bo) {
  BinaryOperator *op1 = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  op1 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(0), op1, "", bo);
  BinaryOperator *op = BinaryOperator::Create(
      Instruction::Xor, bo->getOperand(0), bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Add, bo->getOperand(0), op, "", bo);
  op = BinaryOperator::Create(Instruction::Sub, op, op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a | b => a = (b + c + 1) + ~(c & b)
static void orSubstitution3(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(bo->getType(), 1);
  BinaryOperator *op1 = BinaryOperator::Create(
      Instruction::And, bo->getOperand(1), bo->getOperand(0), "", bo);
  op1 = BinaryOperator::CreateNot(op1, "", bo);
  BinaryOperator *op = BinaryOperator::Create(
      Instruction::Add, bo->getOperand(0), bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Add, op, co, "", bo);
  op = BinaryOperator::Create(Instruction::Add, op, op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b | c => a = (((~a & r) | (a & ~r)) ^ ((~b & r) | (b &
// ~r))) | (~(~a | ~b) & (r | ~r))
static void orSubstitutionRand(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(
      bo->getType(), llvm::cryptoutils->get_uint64_t());
  BinaryOperator *op = BinaryOperator::CreateNot(bo->getOperand(0), "", bo);
  BinaryOperator *op1 = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  BinaryOperator *op2 = BinaryOperator::CreateNot(co, "", bo);
  BinaryOperator *op3 =
      BinaryOperator::Create(Instruction::And, op, co, "", bo);
  BinaryOperator *op4 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(0), op2, "", bo);
  BinaryOperator *op5 =
      BinaryOperator::Create(Instruction::And, op1, co, "", bo);
  BinaryOperator *op6 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(1), op2, "", bo);
  op3 = BinaryOperator::Create(Instruction::Or, op3, op4, "", bo);
  op4 = BinaryOperator::Create(Instruction::Or, op5, op6, "", bo);
  op5 = BinaryOperator::Create(Instruction::Xor, op3, op4, "", bo);
  op3 = BinaryOperator::Create(Instruction::Or, op, op1, "", bo);
  op3 = BinaryOperator::CreateNot(op3, "", bo);
  op4 = BinaryOperator::Create(Instruction::Or, co, op2, "", bo);
  op4 = BinaryOperator::Create(Instruction::And, op3, op4, "", bo);
  op = BinaryOperator::Create(Instruction::Or, op5, op4, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a | b => a = Nor(Nor(a, b), Nor(a, b))
static void orNor(BinaryOperator *bo) {
  BinaryOperator *norab = buildNor(bo->getOperand(0), bo->getOperand(1), bo);
  BinaryOperator *norab2 = buildNor(bo->getOperand(0), bo->getOperand(1), bo);
  BinaryOperator *op = buildNor(norab, norab2, bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a | b => a = Nand(Nand(a, a), Nand(b, b))
static void orNand(BinaryOperator *bo) {
  BinaryOperator *nandaa = buildNand(bo->getOperand(0), bo->getOperand(0), bo);
  BinaryOperator *nandbb = buildNand(bo->getOperand(1), bo->getOperand(1), bo);
  BinaryOperator *op = buildNand(nandaa, nandbb, bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a ^ b => a = (~a & b) | (a & ~b)
static void xorSubstitution(BinaryOperator *bo) {
  BinaryOperator *op = BinaryOperator::CreateNot(bo->getOperand(0), "", bo);
  op = BinaryOperator::Create(Instruction::And, bo->getOperand(1), op, "", bo);
  BinaryOperator *op1 = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  op1 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(0), op1, "", bo);
  op = BinaryOperator::Create(Instruction::Or, op, op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a ^ b => a = (b + c) - 2 * (b & c)
static void xorSubstitution2(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(bo->getType(), 2);
  BinaryOperator *op1 = BinaryOperator::Create(
      Instruction::And, bo->getOperand(0), bo->getOperand(1), "", bo);
  op1 = BinaryOperator::Create(Instruction::Mul, co, op1, "", bo);
  BinaryOperator *op = BinaryOperator::Create(
      Instruction::Add, bo->getOperand(0), bo->getOperand(1), "", bo);
  op = BinaryOperator::Create(Instruction::Sub, op, op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a ^ b => a = b - (2 * (c & ~(b ^ c)) - c)
static void xorSubstitution3(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(bo->getType(), 2);
  BinaryOperator *op1 = BinaryOperator::Create(
      Instruction::Xor, bo->getOperand(0), bo->getOperand(1), "", bo);
  op1 = BinaryOperator::CreateNot(op1, "", bo);
  op1 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(1), op1, "", bo);
  op1 = BinaryOperator::Create(Instruction::Mul, co, op1, "", bo);
  op1 =
      BinaryOperator::Create(Instruction::Sub, op1, bo->getOperand(1), "", bo);
  BinaryOperator *op =
      BinaryOperator::Create(Instruction::Sub, bo->getOperand(0), op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a ^ b <=> (a ^ r) ^ (b ^ r) <=> (~a & r | a & ~r) ^ (~b
// & r | b & ~r) note : r is a random number
static void xorSubstitutionRand(BinaryOperator *bo) {
  ConstantInt *co = (ConstantInt *)ConstantInt::get(
      bo->getType(), llvm::cryptoutils->get_uint64_t());
  BinaryOperator *op = BinaryOperator::CreateNot(bo->getOperand(0), "", bo);
  op = BinaryOperator::Create(Instruction::And, co, op, "", bo);
  BinaryOperator *opr = BinaryOperator::CreateNot(co, "", bo);
  BinaryOperator *op1 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(0), opr, "", bo);
  BinaryOperator *op2 = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  op2 = BinaryOperator::Create(Instruction::And, op2, co, "", bo);
  BinaryOperator *op3 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(1), opr, "", bo);
  op = BinaryOperator::Create(Instruction::Or, op, op1, "", bo);
  op1 = BinaryOperator::Create(Instruction::Or, op2, op3, "", bo);
  op = BinaryOperator::Create(Instruction::Xor, op, op1, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a ^ b => a = Nor(Nor(Nor(a, a), Nor(b, b)), Nor(a, b))
static void xorNor(BinaryOperator *bo) {
  BinaryOperator *noraa = buildNor(bo->getOperand(0), bo->getOperand(0), bo);
  BinaryOperator *norbb = buildNor(bo->getOperand(1), bo->getOperand(1), bo);
  BinaryOperator *nornoraanorbb = buildNor(noraa, norbb, bo);
  BinaryOperator *norab = buildNor(bo->getOperand(0), bo->getOperand(1), bo);
  BinaryOperator *op = buildNor(nornoraanorbb, norab, bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = a ^ b => a = Nand(Nand(Nand(a, a), b), Nand(a, Nand(b,
// b)))
static void xorNand(BinaryOperator *bo) {
  BinaryOperator *nandaa = buildNand(bo->getOperand(0), bo->getOperand(0), bo);
  BinaryOperator *nandnandaab = buildNand(nandaa, bo->getOperand(1), bo);
  BinaryOperator *nandbb = buildNand(bo->getOperand(1), bo->getOperand(1), bo);
  BinaryOperator *nandanandbb = buildNand(bo->getOperand(0), nandbb, bo);
  BinaryOperator *op = buildNand(nandnandaab, nandanandbb, bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b * c => a = (((b | c) * (b & c)) + ((b & ~c) * (c &
// ~b)))
static void mulSubstitution(BinaryOperator *bo) {
  BinaryOperator *op1 = BinaryOperator::CreateNot(bo->getOperand(0), "", bo);
  op1 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(1), op1, "", bo);
  BinaryOperator *op2 = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  op2 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(0), op2, "", bo);
  BinaryOperator *op =
      BinaryOperator::Create(Instruction::Mul, op2, op1, "", bo);
  op1 = BinaryOperator::Create(Instruction::And, bo->getOperand(0),
                               bo->getOperand(1), "", bo);
  op2 = BinaryOperator::Create(Instruction::Or, bo->getOperand(0),
                               bo->getOperand(1), "", bo);
  BinaryOperator *op3 =
      BinaryOperator::Create(Instruction::Mul, op2, op1, "", bo);
  op = BinaryOperator::Create(Instruction::Add, op3, op, "", bo);
  bo->replaceAllUsesWith(op);
}

// Implementation of a = b * c => a = (((b | c) * (b & c)) + ((~(b | ~c)) * (b &
// ~c)))
static void mulSubstitution2(BinaryOperator *bo) {
  BinaryOperator *op1 = BinaryOperator::CreateNot(bo->getOperand(1), "", bo);
  BinaryOperator *op2 =
      BinaryOperator::Create(Instruction::And, bo->getOperand(0), op1, "", bo);
  BinaryOperator *op3 =
      BinaryOperator::Create(Instruction::Or, bo->getOperand(0), op1, "", bo);
  op3 = BinaryOperator::CreateNot(op3, "", bo);
  op3 = BinaryOperator::Create(Instruction::Mul, op3, op2, "", bo);
  BinaryOperator *op4 = BinaryOperator::Create(
      Instruction::And, bo->getOperand(0), bo->getOperand(1), "", bo);
  BinaryOperator *op5 = BinaryOperator::Create(
      Instruction::Or, bo->getOperand(0), bo->getOperand(1), "", bo);
  op5 = BinaryOperator::Create(Instruction::Mul, op5, op4, "", bo);
  BinaryOperator *op =
      BinaryOperator::Create(Instruction::Add, op5, op3, "", bo);
  bo->replaceAllUsesWith(op);
}

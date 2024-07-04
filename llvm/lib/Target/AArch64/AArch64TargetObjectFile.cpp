//===-- AArch64TargetObjectFile.cpp - AArch64 Object Info -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AArch64TargetObjectFile.h"
#include "AArch64TargetMachine.h"
#include "MCTargetDesc/AArch64MCExpr.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCValue.h"
using namespace llvm;
using namespace dwarf;

void AArch64_ELFTargetObjectFile::Initialize(MCContext &Ctx,
                                             const TargetMachine &TM) {
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);
  // AARCH64 ELF ABI does not define static relocation type for TLS offset
  // within a module.  Do not generate AT_location for TLS variables.
  SupportDebugThreadLocalLocation = false;
}

const MCExpr *AArch64_ELFTargetObjectFile::getIndirectSymViaGOTPCRel(
    const GlobalValue *GV, const MCSymbol *Sym, const MCValue &MV,
    int64_t Offset, MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  int64_t FinalOffset = Offset + MV.getConstant();
  const MCExpr *Res =
      MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_GOTPCREL, getContext());
  const MCExpr *Off = MCConstantExpr::create(FinalOffset, getContext());
  return MCBinaryExpr::createAdd(Res, Off, getContext());
}

AArch64_MachoTargetObjectFile::AArch64_MachoTargetObjectFile() {
  SupportGOTPCRelWithOffset = false;
}

const MCExpr *AArch64_MachoTargetObjectFile::getTTypeGlobalReference(
    const GlobalValue *GV, unsigned Encoding, const TargetMachine &TM,
    MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  // On Darwin, we can reference dwarf symbols with foo@GOT-., which
  // is an indirect pc-relative reference. The default implementation
  // won't reference using the GOT, so we need this target-specific
  // version.
  if (Encoding & (DW_EH_PE_indirect | DW_EH_PE_pcrel)) {
    const MCSymbol *Sym = TM.getSymbol(GV);
    const MCExpr *Res =
        MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_GOT, getContext());
    MCSymbol *PCSym = getContext().createTempSymbol();
    Streamer.emitLabel(PCSym);
    const MCExpr *PC = MCSymbolRefExpr::create(PCSym, getContext());
    return MCBinaryExpr::createSub(Res, PC, getContext());
  }

  return TargetLoweringObjectFileMachO::getTTypeGlobalReference(
      GV, Encoding, TM, MMI, Streamer);
}

MCSymbol *AArch64_MachoTargetObjectFile::getCFIPersonalitySymbol(
    const GlobalValue *GV, const TargetMachine &TM,
    MachineModuleInfo *MMI) const {
  return TM.getSymbol(GV);
}

const MCExpr *AArch64_MachoTargetObjectFile::getIndirectSymViaGOTPCRel(
    const GlobalValue *GV, const MCSymbol *Sym, const MCValue &MV,
    int64_t Offset, MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  assert((Offset+MV.getConstant() == 0) &&
         "Arch64 does not support GOT PC rel with extra offset");
  // On ARM64 Darwin, we can reference symbols with foo@GOT-., which
  // is an indirect pc-relative reference.
  const MCExpr *Res =
      MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_GOT, getContext());
  MCSymbol *PCSym = getContext().createTempSymbol();
  Streamer.emitLabel(PCSym);
  const MCExpr *PC = MCSymbolRefExpr::create(PCSym, getContext());
  return MCBinaryExpr::createSub(Res, PC, getContext());
}

void AArch64_MachoTargetObjectFile::getNameWithPrefix(
    SmallVectorImpl<char> &OutName, const GlobalValue *GV,
    const TargetMachine &TM) const {
  // AArch64 does not use section-relative relocations so any global symbol must
  // be accessed via at least a linker-private symbol.
  getMangler().getNameWithPrefix(OutName, GV, /* CannotUsePrivateLabel */ true);
}

template <typename MachineModuleInfoTarget>
static MCSymbol *getAuthPtrSlotSymbolHelper(
    MCContext &Ctx, const TargetMachine &TM, MachineModuleInfo *MMI,
    MachineModuleInfoTarget &TargetMMI, const MCSymbol *RawSym,
    int64_t RawSymOffset, AArch64PACKey::ID Key, uint16_t Discriminator,
    bool HasAddressDiversity) {
  const DataLayout &DL = MMI->getModule()->getDataLayout();

  // Mangle the offset into the stub name.  Avoid '-' in symbols and extra logic
  // by using the uint64_t representation for negative numbers.
  uint64_t OffsetV = RawSymOffset;
  std::string Suffix = "$";
  if (OffsetV)
    Suffix += utostr(OffsetV) + "$";
  Suffix += (Twine("auth_ptr$") + AArch64PACKeyIDToString(Key) + "$" +
             utostr(Discriminator))
                .str();

  if (HasAddressDiversity)
    Suffix += "$addr";

  MCSymbol *StubSym = Ctx.getOrCreateSymbol(DL.getLinkerPrivateGlobalPrefix() +
                                            RawSym->getName() + Suffix);

  typename MachineModuleInfoTarget::AuthStubInfo &StubInfo =
      TargetMMI.getAuthPtrStubEntry(StubSym);

  if (StubInfo.AuthPtrRef)
    return StubSym;

  const MCExpr *Sym = MCSymbolRefExpr::create(RawSym, Ctx);

  // If there is an addend, turn that into the appropriate MCExpr.
  if (RawSymOffset > 0)
    Sym = MCBinaryExpr::createAdd(
        Sym, MCConstantExpr::create(RawSymOffset, Ctx), Ctx);
  else if (RawSymOffset < 0)
    Sym = MCBinaryExpr::createSub(
        Sym, MCConstantExpr::create(-RawSymOffset, Ctx), Ctx);

  StubInfo.AuthPtrRef = AArch64AuthMCExpr::create(Sym, Discriminator, Key,
                                                  HasAddressDiversity, Ctx);
  return StubSym;
}

MCSymbol *AArch64_ELFTargetObjectFile::getAuthPtrSlotSymbol(
    const TargetMachine &TM, MachineModuleInfo *MMI, const MCSymbol *RawSym,
    AArch64PACKey::ID Key, uint16_t Discriminator) const {
  auto &ELFMMI = MMI->getObjFileInfo<MachineModuleInfoELF>();
  // ELF only uses the auth_ptr stub for extern_weak, which disallows offsets
  // and doesn't need address diversity.
  return getAuthPtrSlotSymbolHelper(getContext(), TM, MMI, ELFMMI, RawSym,
                                    /*RawSymOffset=*/0, Key, Discriminator,
                                    /*HasAddressDiversity=*/false);
}

MCSymbol *AArch64_MachoTargetObjectFile::getAuthPtrSlotSymbol(
    const TargetMachine &TM, MachineModuleInfo *MMI, const MCSymbol *RawSym,
    int64_t RawSymOffset, AArch64PACKey::ID Key, uint16_t Discriminator,
    bool HasAddressDiversity) const {
  auto &MachOMMI = MMI->getObjFileInfo<MachineModuleInfoMachO>();
  return getAuthPtrSlotSymbolHelper(getContext(), TM, MMI, MachOMMI, RawSym,
                                    RawSymOffset, Key, Discriminator,
                                    HasAddressDiversity);
}

MCSymbol *AArch64_MachoTargetObjectFile::getAuthPtrSlotSymbol(
    const TargetMachine &TM, MachineModuleInfo *MMI,
    const ConstantPtrAuth &CPA) const {
  const DataLayout &DL = MMI->getModule()->getDataLayout();

  // Figure out the base symbol and the addend, if any.
  APInt Offset(64, 0);
  const Value *BaseGV = CPA.getPointer()->stripAndAccumulateConstantOffsets(
      DL, Offset, /*AllowNonInbounds=*/true);

  auto *BaseGVB = dyn_cast<GlobalValue>(BaseGV);

  // If we can't understand the referenced ConstantExpr, there's nothing
  // else we can do: emit an error.
  if (!BaseGVB) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    OS << "Couldn't resolve target base/addend of ptrauth constant";
    BaseGVB->getContext().emitError(OS.str());
  }

  uint16_t Discriminator = CPA.getDiscriminator()->getZExtValue();

  if (CPA.hasAddressDiscriminator()) {
    std::string Buf;
    raw_string_ostream OS(Buf);
    OS << "Invalid instruction reference to address-diversified ptrauth constant";
    BaseGVB->getContext().emitError(OS.str());
  }

  auto *KeyC = CPA.getKey();
  assert(isUInt<2>(KeyC->getZExtValue()) && "Invalid PAC Key ID");
  uint32_t Key = KeyC->getZExtValue();
  int64_t OffsetV = Offset.getSExtValue();

  return getAuthPtrSlotSymbol(TM, MMI, TM.getSymbol(BaseGVB), OffsetV,
                              AArch64PACKey::ID(Key), Discriminator);
}

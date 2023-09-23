//===- CompileJobCacheResult.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/CompileJobCacheResult.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::cas;
using namespace llvm::cas;
using llvm::Error;

ArrayRef<CompileJobCacheResult::OutputKind>
CompileJobCacheResult::getAllOutputKinds() {
  static const OutputKind OutputKinds[] = {OutputKind::MainOutput,
                                           OutputKind::SerializedDiagnostics,
                                           OutputKind::Dependencies};
  return llvm::makeArrayRef(OutputKinds);
}

Error CompileJobCacheResult::forEachOutput(
    llvm::function_ref<Error(Output)> Callback) const {
  size_t Count = getNumOutputs();
  for (size_t I = 0; I < Count; ++I) {
    OutputKind Kind = getOutputKind(I);
    ObjectRef Object = getOutputObject(I);
    if (auto Err = Callback({Object, Kind}))
      return Err;
  }
  return Error::success();
}

Error CompileJobCacheResult::forEachLoadedOutput(
    llvm::function_ref<Error(Output, std::optional<ObjectProxy>)> Callback) {
  // Kick-off materialization for all outputs concurrently.
  SmallVector<std::future<AsyncProxyValue>, 4> FutureOutputs;
  size_t Count = getNumOutputs();
  for (size_t I = 0; I < Count; ++I) {
    ObjectRef Ref = getOutputObject(I);
    FutureOutputs.push_back(getCAS().getProxyAsync(Ref));
  }

  // Make sure all the outputs have materialized.
  std::optional<Error> OccurredError;
  SmallVector<std::optional<ObjectProxy>, 4> Outputs;
  for (auto &FutureOutput : FutureOutputs) {
    auto Obj = FutureOutput.get().take();
    if (!Obj) {
      if (!OccurredError)
        OccurredError = Obj.takeError();
      else
        OccurredError =
            llvm::joinErrors(std::move(*OccurredError), Obj.takeError());
      continue;
    }
    Outputs.push_back(*Obj);
  }
  if (OccurredError)
    return std::move(*OccurredError);

  // Pass the loaded outputs.
  for (size_t I = 0; I < Count; ++I) {
    if (auto Err = Callback({getOutputObject(I), getOutputKind(I)}, Outputs[I]))
      return Err;
  }
  return Error::success();
}

Optional<CompileJobCacheResult::Output>
CompileJobCacheResult::getOutput(OutputKind Kind) const {
  size_t Count = getNumOutputs();
  for (size_t I = 0; I < Count; ++I) {
    OutputKind K = getOutputKind(I);
    if (Kind == K)
      return Output{getOutputObject(I), Kind};
  }
  return None;
}

StringRef CompileJobCacheResult::getOutputKindName(OutputKind Kind) {
  switch (Kind) {
  case OutputKind::MainOutput:
    return "main";
  case OutputKind::SerializedDiagnostics:
    return "deps";
  case OutputKind::Dependencies:
    return "diags";
  }
}

Error CompileJobCacheResult::print(llvm::raw_ostream &OS) {
  return forEachOutput([&](Output O) -> Error {
    OS << getOutputKindName(O.Kind) << "    " << getCAS().getID(O.Object)
       << '\n';
    return Error::success();
  });
}

size_t CompileJobCacheResult::getNumOutputs() const { return getData().size(); }
ObjectRef CompileJobCacheResult::getOutputObject(size_t I) const {
  return getReference(I);
}
CompileJobCacheResult::OutputKind
CompileJobCacheResult::getOutputKind(size_t I) const {
  return static_cast<OutputKind>(getData()[I]);
}

CompileJobCacheResult::CompileJobCacheResult(const ObjectProxy &Obj)
    : ObjectProxy(Obj) {}

struct CompileJobCacheResult::Builder::PrivateImpl {
  SmallVector<ObjectRef> Objects;
  SmallVector<OutputKind> Kinds;

  struct KindMap {
    OutputKind Kind;
    std::string Path;
  };
  SmallVector<KindMap> KindMaps;
};

CompileJobCacheResult::Builder::Builder() : Impl(*new PrivateImpl) {}
CompileJobCacheResult::Builder::~Builder() { delete &Impl; }

void CompileJobCacheResult::Builder::addKindMap(OutputKind Kind,
                                                StringRef Path) {
  Impl.KindMaps.push_back({Kind, std::string(Path)});
}
void CompileJobCacheResult::Builder::addOutput(OutputKind Kind,
                                               ObjectRef Object) {
  Impl.Kinds.push_back(Kind);
  Impl.Objects.push_back(Object);
}
Error CompileJobCacheResult::Builder::addOutput(StringRef Path,
                                                ObjectRef Object) {
  Impl.Objects.push_back(Object);
  for (auto &KM : Impl.KindMaps) {
    if (KM.Path == Path) {
      Impl.Kinds.push_back(KM.Kind);
      return Error::success();
    }
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "cached output file has unknown path '" +
                                     Path + "'");
}

Expected<ObjectRef> CompileJobCacheResult::Builder::build(ObjectStore &CAS) {
  CompileJobResultSchema Schema(CAS);
  // The resulting Refs contents are:
  // Object 0...N, SchemaKind
  SmallVector<ObjectRef> Refs;
  std::swap(Impl.Objects, Refs);
  Refs.push_back(Schema.getKindRef());
  return CAS.store(Refs, {(char *)Impl.Kinds.begin(), Impl.Kinds.size()});
}

static constexpr llvm::StringLiteral CompileJobResultSchemaName =
    "llvm::cas::schema::compile_job_result::v1";

char CompileJobResultSchema::ID = 0;

CompileJobResultSchema::CompileJobResultSchema(ObjectStore &CAS)
    : CompileJobResultSchema::RTTIExtends(CAS),
      KindRef(
          llvm::cantFail(CAS.storeFromString({}, CompileJobResultSchemaName))) {
}

Expected<CompileJobCacheResult>
CompileJobResultSchema::load(ObjectRef Ref) const {
  auto Proxy = CAS.getProxy(Ref);
  if (!Proxy)
    return Proxy.takeError();
  if (!isNode(*Proxy))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "not a compile job result");
  return CompileJobCacheResult(*Proxy);
}

bool CompileJobResultSchema::isRootNode(const ObjectProxy &Node) const {
  return isNode(Node);
}

bool CompileJobResultSchema::isNode(const ObjectProxy &Node) const {
  size_t N = Node.getNumReferences();
  return N && Node.getReference(N - 1) == getKindRef();
}

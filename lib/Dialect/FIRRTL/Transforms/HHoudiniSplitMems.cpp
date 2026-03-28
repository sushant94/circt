//===- HHoudiniSplitMems.cpp - Split memories into row modules ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass is intended for the modular BTOR2++ flow. It rewrites supported
// FIRRTL memories into wrapper modules containing one row-storage instance per
// memory entry. The pass must run after FlattenMemory and before LowerTypes so
// that memory element types are already flattened to UInt while memory-port
// bundles are still intact.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/FIRRTL/AnnotationDetails.h"
#include "circt/Dialect/FIRRTL/FIRRTLAnnotations.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLUtils.h"
#include "circt/Dialect/FIRRTL/Namespace.h"
#include "circt/Dialect/FIRRTL/Passes.h"
#include "circt/Dialect/HW/HWAttributes.h"
#include "circt/Dialect/Seq/SeqAttributes.h"
#include "mlir/IR/ImplicitLocOpBuilder.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"

namespace circt {
namespace firrtl {
#define GEN_PASS_DEF_HHOUDINISPLITMEMS
#include "circt/Dialect/FIRRTL/Passes.h.inc"
} // namespace firrtl
} // namespace circt

using namespace circt;
using namespace firrtl;

namespace {

struct SupportedMemoryInfo {
  MemOp mem;
  SmallVector<size_t> readPortIndices;
  SmallVector<size_t> writePortIndices;
  Value clockSource;
  bool isSyncRead;
};

class HHoudiniSplitMemsPass
    : public circt::firrtl::impl::HHoudiniSplitMemsBase<
          HHoudiniSplitMemsPass> {
public:
  void runOnOperation() override;

private:
  LogicalResult rewriteModule(FModuleOp module);
  FailureOr<SupportedMemoryInfo> analyzeMemory(MemOp mem);

  FModuleOp createRowModule(CircuitOp circuit, CircuitNamespace &circuitNS,
                            SupportedMemoryInfo &info);
  FModuleOp createWrapperModule(CircuitOp circuit, CircuitNamespace &circuitNS,
                                SupportedMemoryInfo &info,
                                FModuleOp rowModule);
  LogicalResult replaceMemory(SupportedMemoryInfo &info, FModuleOp wrapperModule);

  static SubfieldOp findFirstSubfieldUser(Value value, StringRef fieldName);
  static Value getUniqueConnectedSource(SubfieldOp field);
  static Value getMaskValue(ImplicitLocOpBuilder &builder, Value bundle);
};

SubfieldOp HHoudiniSplitMemsPass::findFirstSubfieldUser(Value value,
                                                        StringRef fieldName) {
  for (auto *user : value.getUsers())
    if (auto subfield = dyn_cast<SubfieldOp>(user))
      if (subfield.getFieldName() == fieldName)
        return subfield;
  return {};
}

Value HHoudiniSplitMemsPass::getUniqueConnectedSource(SubfieldOp field) {
  Value uniqueSource;
  bool sawNonInvalidSource = false;
  for (auto *user : field.getResult().getUsers()) {
    if (isa<AttachOp, SubfieldOp, SubaccessOp, SubindexOp>(user))
      return {};
    auto candidate = dyn_cast<FConnectLike>(user);
    if (!candidate || candidate.getDest() != field.getResult())
      continue;
    auto source = candidate.getSrc();
    if (!source)
      return {};
    if (auto opResult = dyn_cast<OpResult>(source))
      if (isa<InvalidValueOp>(opResult.getOwner()))
        continue;
    if (!sawNonInvalidSource) {
      uniqueSource = source;
      sawNonInvalidSource = true;
      continue;
    }
    if (uniqueSource != source)
      return {};
  }
  if (!sawNonInvalidSource)
    return {};
  return uniqueSource;
}

FailureOr<SupportedMemoryInfo>
HHoudiniSplitMemsPass::analyzeMemory(MemOp mem) {
  if (mem.getInitAttr())
    return failure();
  if (mem.getInnerSymAttr())
    return failure();
  if (!AnnotationSet(mem).empty())
    return failure();

  auto dataType = type_dyn_cast<UIntType>(mem.getDataType());
  if (!dataType || !dataType.hasWidth() || dataType.getWidth() == 0)
    return failure();

  size_t numReads = 0, numWrites = 0, numReadWrites = 0, numDebugs = 0;
  SmallVector<size_t> readPortIndices;
  SmallVector<size_t> writePortIndices;
  for (size_t i = 0, e = mem.getNumResults(); i != e; ++i) {
    if (!AnnotationSet::forPort(mem, i).empty())
      return failure();
    if (type_isa<RefType>(mem.getResult(i).getType()))
      return failure();

    switch (mem.getPortKind(i)) {
    case MemOp::PortKind::Read:
      ++numReads;
      readPortIndices.push_back(i);
      break;
    case MemOp::PortKind::Write:
      ++numWrites;
      writePortIndices.push_back(i);
      break;
    case MemOp::PortKind::ReadWrite:
      ++numReadWrites;
      break;
    case MemOp::PortKind::Debug:
      ++numDebugs;
      break;
    }
  }

  if (numDebugs != 0)
    return failure();
  if (numReads == 0 || numWrites == 0 || numReadWrites != 0)
    return failure();
  if (mem.getWriteLatency() != 1)
    return failure();
  if (mem.getMaskBits() != 1)
    return failure();
  for (auto portIndex : writePortIndices) {
    auto bundleType = type_cast<BundleType>(mem.getResult(portIndex).getType());
    if (auto maskField = bundleType.getElement("mask")) {
      auto maskType = type_dyn_cast<UIntType>(maskField->type);
      if (!maskType || !maskType.hasWidth() || maskType.getWidth() != 1)
        return failure();
    }
    if (auto wmaskField = bundleType.getElement("wmask")) {
      auto maskType = type_dyn_cast<UIntType>(wmaskField->type);
      if (!maskType || !maskType.hasWidth() || maskType.getWidth() != 1)
        return failure();
    }
  }

  bool isSyncRead = false;
  if (mem.getReadLatency() == 0) {
    if (mem.getRuw() != RUWAttr::Undefined)
      return failure();
  } else if (mem.getReadLatency() == 1) {
    isSyncRead = true;
    if (mem.getRuw() != RUWAttr::Old)
      return failure();
  } else {
    return failure();
  }

  Value clockSource;
  auto checkPortClock = [&](size_t portIndex) -> LogicalResult {
    auto clockField = findFirstSubfieldUser(mem.getResult(portIndex), "clk");
    if (!clockField)
      return failure();

    auto clock = getUniqueConnectedSource(clockField);
    if (!clock)
      return failure();

    if (!clockSource) {
      clockSource = clock;
      return success();
    }
    if (clockSource != clock)
      return failure();
    return success();
  };

  for (auto portIndex : readPortIndices)
    if (failed(checkPortClock(portIndex)))
      return failure();
  for (auto portIndex : writePortIndices)
    if (failed(checkPortClock(portIndex)))
      return failure();

  return SupportedMemoryInfo{mem, std::move(readPortIndices),
                             std::move(writePortIndices), clockSource,
                             isSyncRead};
}

Value HHoudiniSplitMemsPass::getMaskValue(ImplicitLocOpBuilder &builder,
                                          Value bundle) {
  auto bundleType = type_cast<BundleType>(bundle.getType());
  if (bundleType.getElement("mask"))
    return builder.create<SubfieldOp>(bundle, "mask");
  if (bundleType.getElement("wmask"))
    return builder.create<SubfieldOp>(bundle, "wmask");
  return builder.create<ConstantOp>(UIntType::get(builder.getContext(), 1),
                                    APInt(1, 1));
}

FModuleOp HHoudiniSplitMemsPass::createRowModule(CircuitOp circuit,
                                                 CircuitNamespace &circuitNS,
                                                 SupportedMemoryInfo &info) {
  auto *context = circuit.getContext();
  auto dataType = info.mem.getDataType();
  OpBuilder b(info.mem->getParentOfType<FModuleOp>());

  SmallVector<PortInfo> ports;
  ports.emplace_back(StringAttr::get(context, "clk"), ClockType::get(context),
                     Direction::In);
  ports.emplace_back(StringAttr::get(context, "wen"),
                     UIntType::get(context, 1), Direction::In);
  ports.emplace_back(StringAttr::get(context, "wdata"), dataType,
                     Direction::In);
  ports.emplace_back(StringAttr::get(context, "q"), dataType, Direction::Out);

  auto moduleName =
      StringAttr::get(context,
                      circuitNS.newName((info.mem.getName() + "_hhoudini_row")
                                            .str()));
  auto rowModule =
      b.create<FModuleOp>(info.mem.getLoc(), moduleName,
                          ConventionAttr::get(context, Convention::Internal),
                          ports);
  SymbolTable::setSymbolVisibility(rowModule, SymbolTable::Visibility::Private);

  ImplicitLocOpBuilder bodyBuilder(info.mem.getLoc(), rowModule.getBodyBlock(),
                                   rowModule.getBodyBlock()->begin());
  auto clk = rowModule.getArgument(0);
  auto wen = rowModule.getArgument(1);
  auto wdata = rowModule.getArgument(2);
  auto q = rowModule.getArgument(3);

  auto state = bodyBuilder.create<RegOp>(dataType, clk, "state");
  emitConnect(bodyBuilder, q, state.getResult());
  bodyBuilder.create<WhenOp>(wen, /*withElseRegion=*/false, [&]() {
    emitConnect(bodyBuilder, state.getResult(), wdata);
  });

  return rowModule;
}

FModuleOp HHoudiniSplitMemsPass::createWrapperModule(
    CircuitOp circuit, CircuitNamespace &circuitNS,
    SupportedMemoryInfo &info, FModuleOp rowModule) {
  auto *context = circuit.getContext();
  OpBuilder b(info.mem->getParentOfType<FModuleOp>());

  SmallVector<PortInfo> ports;
  ports.reserve(info.mem.getNumResults());
  for (size_t i = 0, e = info.mem.getNumResults(); i != e; ++i)
    ports.emplace_back(info.mem.getPortName(i),
                       info.mem.getResult(i).getType(),
                       Direction::In);

  auto moduleName =
      StringAttr::get(context,
                      circuitNS.newName((info.mem.getName() + "_hhoudini_wrap")
                                            .str()));
  auto wrapper =
      b.create<FModuleOp>(info.mem.getLoc(), moduleName,
                          ConventionAttr::get(context, Convention::Internal),
                          ports);
  SymbolTable::setSymbolVisibility(wrapper, SymbolTable::Visibility::Private);

  ImplicitLocOpBuilder bodyBuilder(info.mem.getLoc(), wrapper.getBodyBlock(),
                                   wrapper.getBodyBlock()->begin());
  auto addrBits = static_cast<unsigned>(info.mem.getAddrBits());
  auto addrType = UIntType::get(context, addrBits);

  struct ReadPortSignals {
    Value addr;
    Value en;
    Value clk;
    Value data;
  };
  struct WritePortSignals {
    Value addr;
    Value en;
    Value clk;
    Value data;
    Value mask;
  };

  SmallVector<ReadPortSignals> readPorts;
  readPorts.reserve(info.readPortIndices.size());
  for (auto portIndex : info.readPortIndices) {
    auto readPort = wrapper.getArgument(portIndex);
    readPorts.push_back({bodyBuilder.create<SubfieldOp>(readPort, "addr"),
                         bodyBuilder.create<SubfieldOp>(readPort, "en"),
                         bodyBuilder.create<SubfieldOp>(readPort, "clk"),
                         bodyBuilder.create<SubfieldOp>(readPort, "data")});
  }

  SmallVector<WritePortSignals> writePorts;
  writePorts.reserve(info.writePortIndices.size());
  for (auto portIndex : info.writePortIndices) {
    auto writePort = wrapper.getArgument(portIndex);
    writePorts.push_back({bodyBuilder.create<SubfieldOp>(writePort, "addr"),
                          bodyBuilder.create<SubfieldOp>(writePort, "en"),
                          bodyBuilder.create<SubfieldOp>(writePort, "clk"),
                          bodyBuilder.create<SubfieldOp>(writePort, "data"),
                          getMaskValue(bodyBuilder, writePort)});
  }

  SmallVector<Value> rowOutputs;
  rowOutputs.reserve(info.mem.getDepth());
  for (uint64_t i = 0, e = info.mem.getDepth(); i != e; ++i) {
    auto rowInst = bodyBuilder.create<InstanceOp>(
        info.mem.getLoc(), rowModule, ("row_" + Twine(i)).str());
    auto index = bodyBuilder.create<ConstantOp>(addrType, APInt(addrBits, i));
    Value effectiveWen =
        bodyBuilder.create<ConstantOp>(UIntType::get(context, 1), APInt(1, 0));
    Value effectiveWData = bodyBuilder.create<InvalidValueOp>(info.mem.getDataType());
    for (auto &writePort : writePorts) {
      auto hit = bodyBuilder.create<EQPrimOp>(writePort.addr, index);
      auto maskedEn = bodyBuilder.create<AndPrimOp>(writePort.en, writePort.mask);
      auto portWen = bodyBuilder.create<AndPrimOp>(maskedEn, hit);
      effectiveWData =
          bodyBuilder.create<MuxPrimOp>(portWen, writePort.data, effectiveWData);
      effectiveWen = bodyBuilder.create<OrPrimOp>(effectiveWen, portWen);
    }
    emitConnect(bodyBuilder, rowInst.getResult(0), writePorts.front().clk);
    emitConnect(bodyBuilder, rowInst.getResult(1), effectiveWen);
    emitConnect(bodyBuilder, rowInst.getResult(2), effectiveWData);
    rowOutputs.push_back(rowInst.getResult(3));
  }

  if (!info.isSyncRead) {
    for (auto &readPort : readPorts) {
      Value selected = bodyBuilder.create<InvalidValueOp>(readPort.data.getType());
      for (int64_t i = static_cast<int64_t>(info.mem.getDepth()) - 1; i >= 0;
           --i) {
        auto index = bodyBuilder.create<ConstantOp>(addrType,
                                                    APInt(addrBits, uint64_t(i)));
        auto hit = bodyBuilder.create<EQPrimOp>(readPort.addr, index);
        selected = bodyBuilder.create<MuxPrimOp>(hit, rowOutputs[i], selected);
      }
      emitConnect(bodyBuilder, readPort.data,
                  bodyBuilder.create<InvalidValueOp>(readPort.data.getType()));
      bodyBuilder.create<WhenOp>(readPort.en, /*withElseRegion=*/false, [&]() {
        emitConnect(bodyBuilder, readPort.data, selected);
      });
    }
    return wrapper;
  }

  for (auto [index, readPort] : llvm::enumerate(readPorts)) {
    Value selected =
        bodyBuilder.create<InvalidValueOp>(readPort.data.getType());
    for (int64_t i = static_cast<int64_t>(info.mem.getDepth()) - 1; i >= 0;
         --i) {
      auto rowIndex = bodyBuilder.create<ConstantOp>(
          addrType, APInt(addrBits, uint64_t(i)));
      auto hit = bodyBuilder.create<EQPrimOp>(readPort.addr, rowIndex);
      selected = bodyBuilder.create<MuxPrimOp>(hit, rowOutputs[i], selected);
    }

    auto delayedEnable =
        bodyBuilder
            .create<RegOp>(readPort.en.getType(), readPort.clk,
                           ("read_en_q_" + Twine(index)).str())
            .getResult();
    auto delayedData =
        bodyBuilder
            .create<RegOp>(readPort.data.getType(), readPort.clk,
                           ("read_data_q_" + Twine(index)).str())
            .getResult();
    emitConnect(bodyBuilder, delayedEnable, readPort.en);
    bodyBuilder.create<WhenOp>(readPort.en, /*withElseRegion=*/false, [&]() {
      emitConnect(bodyBuilder, delayedData, selected);
    });
    emitConnect(bodyBuilder, readPort.data,
                bodyBuilder.create<InvalidValueOp>(readPort.data.getType()));
    bodyBuilder.create<WhenOp>(delayedEnable, /*withElseRegion=*/false, [&]() {
      emitConnect(bodyBuilder, readPort.data, delayedData);
    });
  }

  return wrapper;
}

LogicalResult HHoudiniSplitMemsPass::replaceMemory(SupportedMemoryInfo &info,
                                                   FModuleOp wrapperModule) {
  ImplicitLocOpBuilder b(info.mem.getLoc(), info.mem);
  auto inst =
      b.create<InstanceOp>(info.mem.getLoc(), wrapperModule, info.mem.getName(),
                           info.mem.getNameKind(), ArrayRef<Attribute>{},
                           ArrayRef<Attribute>{}, /*lowerToBind=*/false,
                           hw::InnerSymAttr());
  for (auto [oldResult, newResult] :
       llvm::zip(info.mem.getResults(), inst.getResults()))
    oldResult.replaceAllUsesWith(newResult);
  info.mem.erase();
  ++numSplitMems;
  return success();
}

LogicalResult HHoudiniSplitMemsPass::rewriteModule(FModuleOp module) {
  SmallVector<SupportedMemoryInfo> toRewrite;

  module.walk([&](MemOp mem) {
    auto analyzed = analyzeMemory(mem);
    if (succeeded(analyzed))
      toRewrite.push_back(*analyzed);
  });

  if (toRewrite.empty())
    return success();

  auto circuit = module->getParentOfType<CircuitOp>();
  CircuitNamespace circuitNS(circuit);
  for (auto &info : toRewrite) {
    auto rowModule = createRowModule(circuit, circuitNS, info);
    auto wrapperModule = createWrapperModule(circuit, circuitNS, info, rowModule);
    if (failed(replaceMemory(info, wrapperModule)))
      return failure();
  }
  return success();
}

void HHoudiniSplitMemsPass::runOnOperation() {
  auto circuit = getOperation();
  if (!AnnotationSet::hasAnnotation(circuit, hHoudiniSplitMemsAnnoClass)) {
    markAllAnalysesPreserved();
    return;
  }

  for (auto module : circuit.getOps<FModuleOp>())
    if (failed(rewriteModule(module))) {
      signalPassFailure();
      return;
    }
}

} // namespace

std::unique_ptr<mlir::Pass> circt::firrtl::createHHoudiniSplitMemsPass() {
  return std::make_unique<HHoudiniSplitMemsPass>();
}

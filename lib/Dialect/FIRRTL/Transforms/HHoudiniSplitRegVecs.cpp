//===- HHoudiniSplitRegVecs.cpp - Split vector regs into row modules ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass is intended for the modular BTOR2++ flow. It rewrites supported
// top-level FIRRTL vector registers into wrapper modules containing one
// row-storage instance per vector entry. Non-vector registers are intentionally
// left for the normal FIRRTL type-lowering flow.
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/FIRRTL/AnnotationDetails.h"
#include "circt/Dialect/FIRRTL/FIRRTLAnnotations.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "circt/Dialect/FIRRTL/FIRRTLUtils.h"
#include "circt/Dialect/FIRRTL/Namespace.h"
#include "circt/Dialect/FIRRTL/Passes.h"
#include "circt/Dialect/HW/HWAttributes.h"
#include "mlir/IR/ImplicitLocOpBuilder.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"

namespace circt {
namespace firrtl {
#define GEN_PASS_DEF_HHOUDINISPLITREGVECS
#include "circt/Dialect/FIRRTL/Passes.h.inc"
} // namespace firrtl
} // namespace circt

using namespace circt;
using namespace firrtl;

namespace {

struct SupportedRegVecInfo {
  Operation *op;
  Value result;
  Value clock;
  Value reset;
  Value resetValue;
  StringAttr name;
  NameKindEnum nameKind;
  FVectorType vectorType;
  FVectorType resetValueType;
  bool hasReset;
};

class HHoudiniSplitRegVecsPass
    : public circt::firrtl::impl::HHoudiniSplitRegVecsBase<
          HHoudiniSplitRegVecsPass> {
public:
  void runOnOperation() override;

private:
  LogicalResult rewriteModule(FModuleOp module);
  FailureOr<SupportedRegVecInfo> analyzeReg(RegOp reg);
  FailureOr<SupportedRegVecInfo> analyzeRegReset(RegResetOp reg);

  FModuleOp createRowModule(CircuitOp circuit, CircuitNamespace &circuitNS,
                            SupportedRegVecInfo &info);
  FModuleOp createWrapperModule(CircuitOp circuit, CircuitNamespace &circuitNS,
                                SupportedRegVecInfo &info,
                                FModuleOp rowModule);
  LogicalResult replaceRegVec(SupportedRegVecInfo &info,
                              FModuleOp wrapperModule);

  Value cloneAccessPath(OpBuilder &builder, Value oldValue, Value oldRoot,
                        Value newRoot);
  Value getRoot(Value value);
};

static bool hasUnsupportedAttrs(RegOp reg) {
  return !reg.getAnnotationsAttr().empty() || reg.getInnerSymAttr() ||
         reg.isForceable();
}

static bool hasUnsupportedAttrs(RegResetOp reg) {
  return !reg.getAnnotationsAttr().empty() || reg.getInnerSymAttr() ||
         reg.isForceable();
}

static LogicalResult checkVectorType(FVectorType vectorType) {
  if (!vectorType || vectorType.getNumElements() <= 1)
    return failure();

  auto elementType = vectorType.getElementType();
  if (!elementType.isPassive() || elementType.containsAnalog() ||
      hasZeroBitWidth(elementType))
    return failure();

  return success();
}

FailureOr<SupportedRegVecInfo> HHoudiniSplitRegVecsPass::analyzeReg(RegOp reg) {
  if (hasUnsupportedAttrs(reg))
    return failure();

  auto vectorType = type_dyn_cast<FVectorType>(reg.getResult().getType());
  if (failed(checkVectorType(vectorType)))
    return failure();

  return SupportedRegVecInfo{reg.getOperation(),
                             reg.getResult(),
                             reg.getClockVal(),
                             {},
                             {},
                             reg.getNameAttr(),
                             reg.getNameKind(),
                             vectorType,
                             {},
                             false};
}

FailureOr<SupportedRegVecInfo>
HHoudiniSplitRegVecsPass::analyzeRegReset(RegResetOp reg) {
  if (hasUnsupportedAttrs(reg))
    return failure();

  auto vectorType = type_dyn_cast<FVectorType>(reg.getResult().getType());
  if (failed(checkVectorType(vectorType)))
    return failure();

  auto resetValueType = type_dyn_cast<FVectorType>(reg.getResetValue().getType());
  if (!resetValueType ||
      resetValueType.getNumElements() != vectorType.getNumElements())
    return failure();

  return SupportedRegVecInfo{reg.getOperation(),
                             reg.getResult(),
                             reg.getClockVal(),
                             reg.getResetSignal(),
                             reg.getResetValue(),
                             reg.getNameAttr(),
                             reg.getNameKind(),
                             vectorType,
                             resetValueType,
                             true};
}

FModuleOp HHoudiniSplitRegVecsPass::createRowModule(
    CircuitOp circuit, CircuitNamespace &circuitNS, SupportedRegVecInfo &info) {
  auto *context = circuit.getContext();
  OpBuilder b(info.op->getParentOfType<FModuleOp>());
  auto elementType = info.vectorType.getElementType();

  SmallVector<PortInfo> ports;
  ports.emplace_back(StringAttr::get(context, "clk"), ClockType::get(context),
                     Direction::In);
  if (info.hasReset) {
    ports.emplace_back(StringAttr::get(context, "reset"),
                       info.reset.getType(), Direction::In);
    ports.emplace_back(StringAttr::get(context, "reset_value"),
                       info.resetValueType.getElementType(), Direction::In);
  }
  ports.emplace_back(StringAttr::get(context, "d"), elementType, Direction::In);
  ports.emplace_back(StringAttr::get(context, "q"), elementType,
                     Direction::Out);

  auto moduleName = StringAttr::get(
      context,
      circuitNS.newName((Twine(info.name.getValue()) + "_hhoudini_row").str()));
  auto rowModule = b.create<FModuleOp>(
      info.op->getLoc(), moduleName,
      ConventionAttr::get(context, Convention::Internal), ports);
  SymbolTable::setSymbolVisibility(rowModule, SymbolTable::Visibility::Private);

  ImplicitLocOpBuilder bodyBuilder(info.op->getLoc(), rowModule.getBodyBlock(),
                                   rowModule.getBodyBlock()->begin());
  unsigned portIndex = 0;
  auto clk = rowModule.getArgument(portIndex++);
  Value state;
  if (info.hasReset) {
    auto reset = rowModule.getArgument(portIndex++);
    auto resetValue = rowModule.getArgument(portIndex++);
    state = bodyBuilder
                .create<RegResetOp>(elementType, clk, reset, resetValue,
                                    "state")
                .getResult();
  } else {
    state = bodyBuilder.create<RegOp>(elementType, clk, "state").getResult();
  }
  auto d = rowModule.getArgument(portIndex++);
  auto q = rowModule.getArgument(portIndex++);

  emitConnect(bodyBuilder, q, state);
  emitConnect(bodyBuilder, state, d);
  return rowModule;
}

FModuleOp HHoudiniSplitRegVecsPass::createWrapperModule(
    CircuitOp circuit, CircuitNamespace &circuitNS, SupportedRegVecInfo &info,
    FModuleOp rowModule) {
  auto *context = circuit.getContext();
  OpBuilder b(info.op->getParentOfType<FModuleOp>());

  SmallVector<PortInfo> ports;
  ports.emplace_back(StringAttr::get(context, "clk"), ClockType::get(context),
                     Direction::In);
  if (info.hasReset) {
    ports.emplace_back(StringAttr::get(context, "reset"),
                       info.reset.getType(), Direction::In);
    ports.emplace_back(StringAttr::get(context, "reset_value"),
                       info.resetValue.getType(), Direction::In);
  }
  ports.emplace_back(StringAttr::get(context, "d"), info.vectorType,
                     Direction::In);
  ports.emplace_back(StringAttr::get(context, "q"), info.vectorType,
                     Direction::Out);

  auto moduleName = StringAttr::get(
      context,
      circuitNS.newName((info.name.getValue() + "_hhoudini_wrap").str()));
  auto wrapper = b.create<FModuleOp>(
      info.op->getLoc(), moduleName,
      ConventionAttr::get(context, Convention::Internal), ports);
  SymbolTable::setSymbolVisibility(wrapper, SymbolTable::Visibility::Private);

  ImplicitLocOpBuilder bodyBuilder(info.op->getLoc(), wrapper.getBodyBlock(),
                                   wrapper.getBodyBlock()->begin());
  unsigned portIndex = 0;
  auto clk = wrapper.getArgument(portIndex++);
  Value reset;
  Value resetValue;
  if (info.hasReset) {
    reset = wrapper.getArgument(portIndex++);
    resetValue = wrapper.getArgument(portIndex++);
  }
  auto d = wrapper.getArgument(portIndex++);
  auto q = wrapper.getArgument(portIndex++);

  for (uint64_t i = 0, e = info.vectorType.getNumElements(); i != e; ++i) {
    auto rowInst = bodyBuilder.create<InstanceOp>(
        info.op->getLoc(), rowModule, ("row_" + Twine(i)).str());
    unsigned rowPortIndex = 0;
    emitConnect(bodyBuilder, rowInst.getResult(rowPortIndex++), clk);
    if (info.hasReset) {
      emitConnect(bodyBuilder, rowInst.getResult(rowPortIndex++), reset);
      auto resetValueElement =
          bodyBuilder.create<SubindexOp>(resetValue, i).getResult();
      emitConnect(bodyBuilder, rowInst.getResult(rowPortIndex++),
                  resetValueElement);
    }
    auto dElement = bodyBuilder.create<SubindexOp>(d, i).getResult();
    auto qElement = bodyBuilder.create<SubindexOp>(q, i).getResult();
    emitConnect(bodyBuilder, rowInst.getResult(rowPortIndex++), dElement);
    emitConnect(bodyBuilder, qElement, rowInst.getResult(rowPortIndex++));
  }

  return wrapper;
}

Value HHoudiniSplitRegVecsPass::getRoot(Value value) {
  while (auto *def = value.getDefiningOp()) {
    if (auto subindex = dyn_cast<SubindexOp>(def)) {
      value = subindex.getInput();
      continue;
    }
    if (auto subfield = dyn_cast<SubfieldOp>(def)) {
      value = subfield.getInput();
      continue;
    }
    if (auto subaccess = dyn_cast<SubaccessOp>(def)) {
      value = subaccess.getInput();
      continue;
    }
    break;
  }
  return value;
}

Value HHoudiniSplitRegVecsPass::cloneAccessPath(OpBuilder &builder,
                                                Value oldValue, Value oldRoot,
                                                Value newRoot) {
  if (oldValue == oldRoot)
    return newRoot;

  auto *def = oldValue.getDefiningOp();
  if (auto subindex = dyn_cast_or_null<SubindexOp>(def)) {
    auto input = cloneAccessPath(builder, subindex.getInput(), oldRoot, newRoot);
    return builder.create<SubindexOp>(subindex.getLoc(), input,
                                      subindex.getIndex());
  }
  if (auto subfield = dyn_cast_or_null<SubfieldOp>(def)) {
    auto input = cloneAccessPath(builder, subfield.getInput(), oldRoot, newRoot);
    return builder.create<SubfieldOp>(subfield.getLoc(), input,
                                      subfield.getFieldIndex());
  }
  if (auto subaccess = dyn_cast_or_null<SubaccessOp>(def)) {
    auto input =
        cloneAccessPath(builder, subaccess.getInput(), oldRoot, newRoot);
    return builder.create<SubaccessOp>(subaccess.getLoc(), input,
                                       subaccess.getIndex());
  }

  return {};
}

LogicalResult
HHoudiniSplitRegVecsPass::replaceRegVec(SupportedRegVecInfo &info,
                                        FModuleOp wrapperModule) {
  ImplicitLocOpBuilder b(info.op->getLoc(), info.op);
  auto inst = b.create<InstanceOp>(info.op->getLoc(), wrapperModule,
                                   info.name.getValue(), info.nameKind);

  unsigned portIndex = 0;
  auto clk = inst.getResult(portIndex++);
  emitConnect(b, clk, info.clock);
  Value reset;
  Value resetValue;
  if (info.hasReset) {
    reset = inst.getResult(portIndex++);
    resetValue = inst.getResult(portIndex++);
    emitConnect(b, reset, info.reset);
    emitConnect(b, resetValue, info.resetValue);
  }
  auto d = inst.getResult(portIndex++);
  auto q = inst.getResult(portIndex++);
  emitConnect(b, d, q);

  SmallVector<FConnectLike> destConnects;
  auto module = info.op->getParentOfType<FModuleOp>();
  module.walk([&](Operation *op) {
    auto connect = dyn_cast<FConnectLike>(op);
    if (connect && getRoot(connect.getDest()) == info.result)
      destConnects.push_back(connect);
  });

  for (auto connect : destConnects) {
    OpBuilder pathBuilder(connect.getOperation());
    auto newDest =
        cloneAccessPath(pathBuilder, connect.getDest(), info.result, d);
    if (!newDest)
      return failure();
    connect.getOperation()->setOperand(0, newDest);
  }

  info.result.replaceAllUsesWith(q);
  info.op->erase();
  ++numSplitRegVecs;
  return success();
}

LogicalResult HHoudiniSplitRegVecsPass::rewriteModule(FModuleOp module) {
  SmallVector<SupportedRegVecInfo> toRewrite;

  module.walk([&](Operation *op) {
    if (auto reg = dyn_cast<RegOp>(op)) {
      auto analyzed = analyzeReg(reg);
      if (succeeded(analyzed))
        toRewrite.push_back(*analyzed);
      return;
    }
    if (auto reg = dyn_cast<RegResetOp>(op)) {
      auto analyzed = analyzeRegReset(reg);
      if (succeeded(analyzed))
        toRewrite.push_back(*analyzed);
    }
  });

  if (toRewrite.empty())
    return success();

  auto circuit = module->getParentOfType<CircuitOp>();
  CircuitNamespace circuitNS(circuit);
  for (auto &info : toRewrite) {
    auto rowModule = createRowModule(circuit, circuitNS, info);
    auto wrapperModule = createWrapperModule(circuit, circuitNS, info, rowModule);
    if (failed(replaceRegVec(info, wrapperModule)))
      return failure();
  }
  return success();
}

void HHoudiniSplitRegVecsPass::runOnOperation() {
  auto circuit = getOperation();
  if (!AnnotationSet::hasAnnotation(circuit, hHoudiniSplitRegVecsAnnoClass)) {
    markAllAnalysesPreserved();
    return;
  }

  SmallVector<FModuleOp> modules(circuit.getOps<FModuleOp>());
  for (auto module : modules)
    if (failed(rewriteModule(module))) {
      signalPassFailure();
      return;
    }
}

} // namespace

std::unique_ptr<mlir::Pass> circt::firrtl::createHHoudiniSplitRegVecsPass() {
  return std::make_unique<HHoudiniSplitRegVecsPass>();
}

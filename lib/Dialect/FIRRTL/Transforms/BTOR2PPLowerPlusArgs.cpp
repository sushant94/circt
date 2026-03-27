//===- BTOR2PPLowerPlusArgs.cpp - Rewrite plusargs for BTOR2 flows --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLTypes.h"
#include "circt/Dialect/FIRRTL/FIRRTLUtils.h"
#include "circt/Dialect/FIRRTL/Passes.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"

namespace circt {
namespace firrtl {
#define GEN_PASS_DEF_BTOR2LOWERPLUSARGS
#define GEN_PASS_DEF_BTOR2PPLOWERPLUSARGS
#include "circt/Dialect/FIRRTL/Passes.h.inc"
} // namespace firrtl
} // namespace circt

using namespace circt;
using namespace firrtl;

namespace {
class BTOR2LowerPlusArgsPassCommon {
public:
  explicit BTOR2LowerPlusArgsPassCommon(CircuitOp circuit) : circuit(circuit) {}

  LogicalResult run();

private:
  CircuitOp circuit;

  FailureOr<IntegerAttr> getDefaultValue(FExtModuleOp extModule);
};

class BTOR2LowerPlusArgsPass
    : public circt::firrtl::impl::BTOR2LowerPlusArgsBase<
          BTOR2LowerPlusArgsPass> {
public:
  void runOnOperation() override {
    if (failed(BTOR2LowerPlusArgsPassCommon(getOperation()).run()))
      signalPassFailure();
  }
};

class BTOR2PPLowerPlusArgsPass
    : public circt::firrtl::impl::BTOR2PPLowerPlusArgsBase<
          BTOR2PPLowerPlusArgsPass> {
public:
  void runOnOperation() override {
    if (failed(BTOR2LowerPlusArgsPassCommon(getOperation()).run()))
      signalPassFailure();
  }
};
} // namespace

FailureOr<IntegerAttr>
BTOR2LowerPlusArgsPassCommon::getDefaultValue(FExtModuleOp extModule) {
  if (extModule.getNumPorts() != 1)
    return extModule.emitOpError(
        "expected plusarg_reader extmodule to have exactly one port");

  if (extModule.getPortDirection(0) != Direction::Out)
    return extModule.emitOpError(
        "expected plusarg_reader extmodule port to be an output");

  auto portType = dyn_cast<UIntType>(extModule.getPortType(0));
  if (!portType)
    return extModule.emitOpError(
        "expected plusarg_reader extmodule output to be UInt");
  if (!portType.hasWidth())
    return extModule.emitOpError(
        "expected plusarg_reader extmodule output to have known width");

  ParamDeclAttr defaultParam;
  for (auto param : extModule.getParameters().getAsRange<ParamDeclAttr>())
    if (param.getName().getValue() == "DEFAULT") {
      defaultParam = param;
      break;
    }

  if (!defaultParam)
    return extModule.emitOpError(
        "expected plusarg_reader extmodule to define DEFAULT parameter");

  auto defaultValue = dyn_cast<IntegerAttr>(defaultParam.getValue());
  if (!defaultValue)
    return extModule.emitOpError(
        "expected plusarg_reader DEFAULT parameter to be an integer");

  APInt value = defaultValue.getValue();
  if (value.isNegative())
    return extModule.emitOpError(
        "expected plusarg_reader DEFAULT parameter to be non-negative");

  unsigned width = *portType.getWidth();
  if (value.getActiveBits() > width)
    return extModule.emitOpError()
           << "expected plusarg_reader DEFAULT parameter to fit in UInt<"
           << width << ">";

  return getIntAttr(portType, value.zextOrTrunc(width));
}

LogicalResult BTOR2LowerPlusArgsPassCommon::run() {
  DenseMap<StringAttr, IntegerAttr> plusArgDefaults;
  SmallVector<FExtModuleOp> plusArgExtModules;
  for (auto extModule : circuit.getOps<FExtModuleOp>()) {
    auto defname = extModule.getDefnameAttr();
    if (!defname || defname.getValue() != "plusarg_reader")
      continue;

    auto defaultValue = getDefaultValue(extModule);
    if (failed(defaultValue))
      return failure();

    plusArgDefaults[extModule.getNameAttr()] = *defaultValue;
    plusArgExtModules.push_back(extModule);
  }

  if (plusArgDefaults.empty()) {
    return success();
  }

  SmallVector<InstanceOp> instancesToErase;
  bool hadError = false;
  circuit.walk([&](InstanceOp inst) -> WalkResult {
    auto it = plusArgDefaults.find(inst.getModuleNameAttr().getAttr());
    if (it == plusArgDefaults.end())
      return WalkResult::advance();

    if (inst.getNumResults() != 1) {
      inst.emitOpError(
          "expected plusarg_reader instance to have exactly one result");
      hadError = true;
      return WalkResult::interrupt();
    }

    OpBuilder builder(inst);
    auto constant = builder.create<ConstantOp>(inst.getLoc(),
                                               inst.getResult(0).getType(),
                                               it->second);
    inst.getResult(0).replaceAllUsesWith(constant);
    instancesToErase.push_back(inst);
    return WalkResult::advance();
  });

  if (hadError) {
    return failure();
  }

  for (auto inst : llvm::reverse(instancesToErase))
    inst.erase();

  for (auto extModule : plusArgExtModules)
    if (SymbolTable::symbolKnownUseEmpty(extModule, circuit))
      extModule.erase();

  return success();
}

std::unique_ptr<mlir::Pass> circt::firrtl::createBTOR2LowerPlusArgsPass() {
  return std::make_unique<BTOR2LowerPlusArgsPass>();
}

std::unique_ptr<mlir::Pass> circt::firrtl::createBTOR2PPLowerPlusArgsPass() {
  return std::make_unique<BTOR2PPLowerPlusArgsPass>();
}

//===- Firtool.cpp - Definitions for the firtool pipeline setup -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "circt/Firtool/Firtool.h"
#include "circt/Conversion/Passes.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/FIRRTL/FIRRTLOps.h"
#include "circt/Dialect/FIRRTL/Passes.h"
#include "circt/Dialect/HW/HWPasses.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/OM/OMPasses.h"
#include "circt/Dialect/SV/SVPasses.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "circt/Dialect/Seq/SeqPasses.h"
#include "circt/Dialect/Verif/VerifPasses.h"
#include "circt/Support/Passes.h"
#include "circt/Transforms/Passes.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace circt;

namespace {

/// A conservative BTOR2/formal-oriented cleanup for register patterns that are
/// otherwise awkward for syntactic COI-based consumers. This pass must remain
/// semantics preserving: it only folds registers whose value is fixed by
/// explicit state semantics, and otherwise only canonicalizes mux shape.
struct BTOR2FormalNormalizeRegsPass
    : public mlir::PassWrapper<BTOR2FormalNormalizeRegsPass,
                               mlir::OperationPass<hw::HWModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(BTOR2FormalNormalizeRegsPass)

  BTOR2FormalNormalizeRegsPass() = default;
  BTOR2FormalNormalizeRegsPass(const BTOR2FormalNormalizeRegsPass &)
      : BTOR2FormalNormalizeRegsPass() {}

  StringRef getArgument() const override {
    return "btor2-formal-normalize-regs";
  }
  StringRef getDescription() const override {
    return "Normalize simple register self-feedback patterns before BTOR2 "
           "emission";
  }

  Statistic numSelfLoopRegsFolded{
      this, "num-self-loop-regs-folded",
      "Number of trivially self-looping registers folded to constants"};
  Statistic numConstSelfLoopRegsFolded{
      this, "num-const-self-loop-regs-folded",
      "Number of initialized constant/self-loop registers folded"};
  Statistic numSelfHoldMuxesCanonicalized{
      this, "num-self-hold-muxes-canonicalized",
      "Number of self-hold muxes canonicalized to hold on the false branch"};
  Statistic numSelfHoldMuxesRecognized{
      this, "num-self-hold-muxes-recognized",
      "Number of self-hold muxes already in canonical form"};

  void runOnOperation() override;

private:
  template <typename RegOp>
  void normalizeSelfHoldMux(RegOp reg, mlir::IRRewriter &rewriter);
};

static void replaceRegWithConstant(mlir::Operation *reg, mlir::Type type,
                                   const APInt &value,
                                   mlir::IRRewriter &rewriter) {
  rewriter.setInsertionPoint(reg);
  auto constant = rewriter.create<hw::ConstantOp>(reg->getLoc(), value);
  rewriter.replaceOpWithNewOp<hw::BitcastOp>(reg, type, constant);
}

static std::optional<APInt> getConstantValue(mlir::Value value) {
  if (!value)
    return std::nullopt;
  if (auto constant = value.getDefiningOp<hw::ConstantOp>())
    return constant.getValue();
  return std::nullopt;
}

static std::optional<APInt> getImmutableConstantValue(mlir::Value value) {
  if (!value)
    return std::nullopt;
  auto immutable = dyn_cast<mlir::TypedValue<seq::ImmutableType>>(value);
  if (!immutable)
    return std::nullopt;
  auto constant =
      seq::unwrapImmutableValue(immutable).getDefiningOp<hw::ConstantOp>();
  if (!constant)
    return std::nullopt;
  return constant.getValue();
}

static bool hasOnlyAllowedAttrs(mlir::Operation *op,
                                ArrayRef<StringRef> allowedAttrs) {
  for (auto attr : op->getAttrs()) {
    if (!llvm::is_contained(allowedAttrs, attr.getName().strref()))
      return false;
  }
  return true;
}

static bool canRewriteFirReg(seq::FirRegOp reg) {
  return !reg.getInnerSymAttr() &&
         hasOnlyAllowedAttrs(reg, {"name", "preset",
                                   "firrtl.random_init_start"});
}

static bool canRewriteCompReg(seq::CompRegOp reg) {
  return !reg.getInnerSymAttr() && hasOnlyAllowedAttrs(reg, {"name"});
}

template <typename RegOp>
void BTOR2FormalNormalizeRegsPass::normalizeSelfHoldMux(
    RegOp reg, mlir::IRRewriter &rewriter) {
  auto mux = reg.getInput().template getDefiningOp<comb::MuxOp>();
  if (!mux)
    return;

  auto regValue = reg.getResult();
  if (mux.getFalseValue() == regValue) {
    ++numSelfHoldMuxesRecognized;
    return;
  }
  if (mux.getTrueValue() != regValue)
    return;

  rewriter.setInsertionPoint(mux);
  auto notCond =
      comb::createOrFoldNot(mux.getLoc(), mux.getCond(), rewriter,
                            /*twoState=*/mux.getTwoState());
  rewriter.replaceOpWithNewOp<comb::MuxOp>(
      mux, notCond, mux.getFalseValue(), mux.getTrueValue(),
      mux.getTwoState());
  ++numSelfHoldMuxesCanonicalized;
}

template <>
void BTOR2FormalNormalizeRegsPass::normalizeSelfHoldMux<seq::FirRegOp>(
    seq::FirRegOp reg, mlir::IRRewriter &rewriter) {
  auto mux = reg.getNext().getDefiningOp<comb::MuxOp>();
  if (!mux)
    return;

  auto regValue = reg.getResult();
  if (mux.getFalseValue() == regValue) {
    ++numSelfHoldMuxesRecognized;
    return;
  }
  if (mux.getTrueValue() != regValue)
    return;

  rewriter.setInsertionPoint(mux);
  auto notCond =
      comb::createOrFoldNot(mux.getLoc(), mux.getCond(), rewriter,
                            /*twoState=*/mux.getTwoState());
  rewriter.replaceOpWithNewOp<comb::MuxOp>(
      mux, notCond, mux.getFalseValue(), mux.getTrueValue(),
      mux.getTwoState());
  ++numSelfHoldMuxesCanonicalized;
}

void BTOR2FormalNormalizeRegsPass::runOnOperation() {
  mlir::IRRewriter rewriter(&getContext());
  SmallVector<seq::FirRegOp> firRegs;
  SmallVector<seq::CompRegOp> compRegs;

  getOperation().walk([&](mlir::Operation *op) {
    if (auto reg = dyn_cast<seq::FirRegOp>(op))
      firRegs.push_back(reg);
    else if (auto reg = dyn_cast<seq::CompRegOp>(op))
      compRegs.push_back(reg);
  });

  for (auto reg : firRegs) {
    if (!canRewriteFirReg(reg))
      continue;

    normalizeSelfHoldMux(reg, rewriter);

    // Match the existing seq.firreg canonicalization, but keep this pass
    // opt-in for formal BTOR2 users.
    if (reg.getResetValue() || reg.getNext() != reg.getResult())
      continue;
    if (auto preset = reg.getPresetAttr())
      if (!preset.getValue().isZero())
        continue;

    auto width = hw::getBitWidth(reg.getType());
    if (width < 0)
      continue;
    replaceRegWithConstant(reg, reg.getType(), APInt::getZero(width),
                           rewriter);
    ++numSelfLoopRegsFolded;
  }

  for (auto reg : compRegs) {
    if (!canRewriteCompReg(reg))
      continue;

    normalizeSelfHoldMux(reg, rewriter);

    if (reg.getInput() != reg.getResult())
      continue;

    auto initValue = getImmutableConstantValue(reg.getInitialValue());
    if (!initValue)
      continue;

    if (auto resetValue = reg.getResetValue()) {
      auto resetConstant = getConstantValue(resetValue);
      if (!resetConstant || *resetConstant != *initValue)
        continue;
    }

    if (hw::getBitWidth(reg.getType()) < 0)
      continue;
    replaceRegWithConstant(reg, reg.getType(), *initValue, rewriter);
    ++numConstSelfLoopRegsFolded;
  }
}

std::unique_ptr<mlir::Pass> createBTOR2FormalNormalizeRegsPass() {
  return std::make_unique<BTOR2FormalNormalizeRegsPass>();
}

} // namespace

LogicalResult firtool::populatePreprocessTransforms(mlir::PassManager &pm,
                                                    const FirtoolOptions &opt) {
  pm.nest<firrtl::CircuitOp>().addPass(
      firrtl::createCheckRecursiveInstantiation());
  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createCheckLayers());
  // Legalize away "open" aggregates to hw-only versions.
  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createLowerOpenAggsPass());

  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createResolvePathsPass());

  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createLowerFIRRTLAnnotationsPass(
      opt.shouldDisableUnknownAnnotations(),
      opt.shouldDisableClasslessAnnotations(),
      opt.shouldLowerNoRefTypePortAnnotations(),
      opt.shouldAllowAddingPortsOnPublic()));

  if (opt.shouldEnableDebugInfo())
    pm.nest<firrtl::CircuitOp>().addNestedPass<firrtl::FModuleOp>(
        firrtl::createMaterializeDebugInfoPass());

  pm.nest<firrtl::CircuitOp>().addPass(
      firrtl::createLowerIntmodulesPass(opt.shouldFixupEICGWrapper()));
  pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
      firrtl::createLowerIntrinsicsPass());

  return success();
}

LogicalResult firtool::populateCHIRRTLToLowFIRRTL(mlir::PassManager &pm,
                                                  const FirtoolOptions &opt,
                                                  StringRef inputFilename) {
  // TODO: Ensure instance graph and other passes can handle instance choice
  // then run this pass after all diagnostic passes have run.
  pm.addNestedPass<firrtl::CircuitOp>(firrtl::createSpecializeOptionPass(
      opt.shouldSelectDefaultInstanceChoice()));
  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createLowerSignaturesPass());

  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createInjectDUTHierarchyPass());

  if (!opt.shouldDisableOptimization()) {
    if (opt.shouldDisableCSEinClasses())
      pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
          mlir::createCSEPass());
    else
      pm.nest<firrtl::CircuitOp>().nestAny().addPass(mlir::createCSEPass());
  }

  pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
      firrtl::createPassiveWiresPass());

  pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
      firrtl::createDropNamesPass(opt.getPreserveMode()));

  pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
      firrtl::createLowerCHIRRTLPass());

  // Run LowerMatches before InferWidths, as the latter does not support the
  // match statement, but it does support what they lower to.
  pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
      firrtl::createLowerMatchesPass());

  // Width inference creates canonicalization opportunities.
  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createInferWidthsPass());

  pm.nest<firrtl::CircuitOp>().addPass(
      firrtl::createMemToRegOfVecPass(opt.shouldReplaceSequentialMemories(),
                                      opt.shouldIgnoreReadEnableMemories(),
                                      opt.shouldEnableModularBTOR2PPMemorySplitting()));

  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createInferResetsPass());

  if (opt.shouldExportChiselInterface()) {
    StringRef outdir = opt.getChiselInterfaceOutputDirectory();
    if (opt.isDefaultOutputFilename() && outdir.empty()) {
      pm.nest<firrtl::CircuitOp>().addPass(createExportChiselInterfacePass());
    } else {
      if (outdir.empty())
        outdir = opt.getOutputFilename();
      pm.nest<firrtl::CircuitOp>().addPass(
          createExportSplitChiselInterfacePass(outdir));
    }
  }

  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createDropConstPass());

  if (opt.shouldDedup())
    pm.nest<firrtl::CircuitOp>().addPass(firrtl::createDedupPass());

  if (opt.shouldConvertVecOfBundle()) {
    pm.addNestedPass<firrtl::CircuitOp>(firrtl::createLowerFIRRTLTypesPass(
        firrtl::PreserveAggregate::All, firrtl::PreserveAggregate::All));
    pm.addNestedPass<firrtl::CircuitOp>(firrtl::createVBToBVPass());
  }

  if (!opt.shouldLowerMemories())
    pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
      firrtl::createFlattenMemoryPass());

  if (opt.shouldEnableModularBTOR2PPMemorySplitting())
    pm.nest<firrtl::CircuitOp>().addPass(firrtl::createHHoudiniSplitMemsPass());

  if (opt.shouldEnableModularBTOR2PPMemorySplitting())
    pm.nest<firrtl::CircuitOp>().addPass(
        firrtl::createMemToRegOfVecFallbackPass(
            opt.shouldIgnoreReadEnableMemories()));

  if (opt.shouldEnableModularBTOR2PPMemorySplitting())
    pm.nest<firrtl::CircuitOp>().addPass(
        firrtl::createHHoudiniSplitRegVecsPass());

  // The input mlir file could be firrtl dialect so we might need to clean
  // things up.
  //  pm.addNestedPass<firrtl::CircuitOp>(firrtl::createLowerSignaturesPass());
  pm.addNestedPass<firrtl::CircuitOp>(firrtl::createLowerFIRRTLTypesPass(
      opt.getPreserveAggregate(), firrtl::PreserveAggregate::None));

  {
    auto &modulePM = pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>();
    modulePM.addPass(firrtl::createExpandWhensPass());
    modulePM.addPass(firrtl::createSFCCompatPass());
  }

  pm.addNestedPass<firrtl::CircuitOp>(firrtl::createCheckCombLoopsPass());

  // Must run this pass after all diagnostic passes have run, otherwise it can
  // hide errors.
  pm.addNestedPass<firrtl::CircuitOp>(firrtl::createSpecializeLayersPass());

  // Run after inference, layer specialization.
  if (opt.shouldConvertProbesToSignals())
    pm.nest<firrtl::CircuitOp>().addPass(firrtl::createProbesToSignalsPass());

  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createInlinerPass());

  pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
      firrtl::createLayerMergePass());

  // Preset the random initialization parameters for each module. The current
  // implementation assumes it can run at a time where every register is
  // currently in the final module it will be emitted in, all registers have
  // been created, and no registers have yet been removed.
  if (opt.isRandomEnabled(FirtoolOptions::RandomKind::Reg))
    pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
        firrtl::createRandomizeRegisterInitPass());

  // If we parsed a FIRRTL file and have optimizations enabled, clean it up.
  if (!opt.shouldDisableOptimization())
    pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
        createSimpleCanonicalizerPass());

  // Run the infer-rw pass, which merges read and write ports of a memory with
  // mutually exclusive enables.
  if (!opt.shouldDisableOptimization())
    pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
        firrtl::createInferReadWritePass());

  if (opt.shouldReplaceSequentialMemories())
    pm.nest<firrtl::CircuitOp>().addPass(firrtl::createLowerMemoryPass());

  if (opt.shouldAddCompanionAssume())
    pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
        circt::firrtl::createCreateCompanionAssume());

  if (!opt.shouldDisableOptimization())
    pm.nest<firrtl::CircuitOp>().addPass(firrtl::createIMConstPropPass());

  pm.addNestedPass<firrtl::CircuitOp>(firrtl::createAddSeqMemPortsPass());

  pm.addPass(firrtl::createCreateSiFiveMetadataPass(
      opt.shouldReplaceSequentialMemories(),
      opt.getReplaceSequentialMemoriesFile()));

  pm.addNestedPass<firrtl::CircuitOp>(firrtl::createExtractInstancesPass());

  // Run SymbolDCE as late as possible, but before InnerSymbolDCE. This is for
  // hierpathop's and just for general cleanup.
  pm.addNestedPass<firrtl::CircuitOp>(mlir::createSymbolDCEPass());

  // Run InnerSymbolDCE as late as possible, but before IMDCE.
  pm.addPass(firrtl::createInnerSymbolDCEPass());

  // The above passes, IMConstProp in particular, introduce additional
  // canonicalization opportunities that we should pick up here before we
  // proceed to output-specific pipelines.
  if (!opt.shouldDisableOptimization()) {
    pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
        createSimpleCanonicalizerPass());
    pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
        circt::firrtl::createRegisterOptimizerPass());
    // Re-run IMConstProp to propagate constants produced by register
    // optimizations.
    pm.nest<firrtl::CircuitOp>().addPass(firrtl::createIMConstPropPass());
    pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
        createSimpleCanonicalizerPass());
    pm.addPass(firrtl::createIMDeadCodeElimPass());
  }

  // Always run this, required for legalization.
  pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
      firrtl::createMergeConnectionsPass(
          !opt.shouldDisableAggressiveMergeConnections()));

  if (!opt.shouldDisableOptimization())
    pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
        firrtl::createVectorizationPass());

  // Layer lowering passes.  Move operations into layers when possible and
  // remove layers by converting them to other constructs.  This lowering
  // process can create a few optimization opportunities.
  //
  // TODO: Improve LowerLayers to avoid the need for canonicalization. See:
  //   https://github.com/llvm/circt/issues/7896
  if (!opt.shouldDisableLayerSink()) {
    if (opt.shouldAdvancedLayerSink())
      pm.nest<firrtl::CircuitOp>().addPass(
          firrtl::createAdvancedLayerSinkPass());
    else
      pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
          firrtl::createLayerSinkPass());
  }
  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createLowerLayersPass());
  if (!opt.shouldDisableOptimization())
    pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
        createSimpleCanonicalizerPass());

  auto outputFilename = opt.getOutputFilename();
  if (outputFilename == "-")
    outputFilename = "";

  pm.nest<firrtl::CircuitOp>().addPass(
      firrtl::createAssignOutputDirsPass(outputFilename));

  // Run passes to resolve Grand Central features.  This should run before
  // BlackBoxReader because Grand Central needs to inform BlackBoxReader where
  // certain black boxes should be placed.  Note: all Grand Central Taps related
  // collateral is resolved entirely by LowerAnnotations.
  // Run this after output directories are (otherwise) assigned,
  // so generated interfaces can be appropriately marked.
  pm.addNestedPass<firrtl::CircuitOp>(
      firrtl::createGrandCentralPass(opt.getCompanionMode()));

  // Read black box source files into the IR.
  StringRef blackBoxRoot = opt.getBlackBoxRootPath().empty()
                               ? llvm::sys::path::parent_path(inputFilename)
                               : opt.getBlackBoxRootPath();
  pm.nest<firrtl::CircuitOp>().addPass(
      firrtl::createBlackBoxReaderPass(blackBoxRoot));
  return success();
}

LogicalResult firtool::populateLowFIRRTLToHW(mlir::PassManager &pm,
                                             const FirtoolOptions &opt) {
  // Remove TraceAnnotations and write their updated paths to an output
  // annotation file.
  pm.nest<firrtl::CircuitOp>().addPass(
      firrtl::createResolveTracesPass(opt.getOutputAnnotationFilename()));

  // Lower the ref.resolve and ref.send ops and remove the RefType ports.
  // LowerToHW cannot handle RefType so, this pass must be run to remove all
  // RefType ports and ops.
  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createLowerXMRPass());

  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createLowerDPIPass());
  pm.nest<firrtl::CircuitOp>().addPass(firrtl::createLowerClassesPass());
  pm.nest<firrtl::CircuitOp>().addPass(om::createVerifyObjectFieldsPass());

  // Check for static asserts.
  pm.nest<firrtl::CircuitOp>().nest<firrtl::FModuleOp>().addPass(
      circt::firrtl::createLintingPass());

  pm.addPass(createLowerFIRRTLToHWPass(opt.shouldEnableAnnotationWarning(),
                                       opt.getVerificationFlavor()));

  if (!opt.shouldDisableOptimization()) {
    auto &modulePM = pm.nest<hw::HWModuleOp>();
    modulePM.addPass(mlir::createCSEPass());
    modulePM.addPass(createSimpleCanonicalizerPass());
  }

  // Check inner symbols and inner refs.
  pm.addPass(hw::createVerifyInnerRefNamespacePass());

  // Check OM object fields.
  pm.addPass(om::createVerifyObjectFieldsPass());

  // Run the verif op verification pass
  pm.addNestedPass<hw::HWModuleOp>(verif::createVerifyClockedAssertLikePass());

  return success();
}

LogicalResult firtool::populateHWToSV(mlir::PassManager &pm,
                                      const FirtoolOptions &opt) {
  pm.nestAny().addPass(verif::createStripContractsPass());
  pm.addPass(verif::createLowerFormalToHWPass());

  if (opt.shouldExtractTestCode())
    pm.addPass(sv::createSVExtractTestCodePass(
        opt.shouldEtcDisableInstanceExtraction(),
        opt.shouldEtcDisableRegisterExtraction(),
        opt.shouldEtcDisableModuleInlining()));

  pm.addPass(seq::createExternalizeClockGatePass(opt.getClockGateOptions()));
  pm.addPass(circt::createLowerSimToSVPass());
  pm.addPass(circt::createLowerSeqToSVPass(
      {/*disableRegRandomization=*/!opt.isRandomEnabled(
           FirtoolOptions::RandomKind::Reg),
       /*disableMemRandomization=*/
       !opt.isRandomEnabled(FirtoolOptions::RandomKind::Mem),
       /*emitSeparateAlwaysBlocks=*/
       opt.shouldEmitSeparateAlwaysBlocks()}));
  pm.addNestedPass<hw::HWModuleOp>(createLowerVerifToSVPass());
  pm.addPass(seq::createHWMemSimImplPass(
      {/*disableMemRandomization=*/!opt.isRandomEnabled(
           FirtoolOptions::RandomKind::Mem),
       /*disableRegRandomization=*/
       !opt.isRandomEnabled(FirtoolOptions::RandomKind::Reg),
       /*replSeqMem=*/opt.shouldReplaceSequentialMemories(),
       /*readEnableMode=*/opt.shouldIgnoreReadEnableMemories()
           ? seq::ReadEnableMode::Ignore
           : seq::ReadEnableMode::Undefined,
       /*addMuxPragmas=*/opt.shouldAddMuxPragmas(),
       /*addVivadoRAMAddressConflictSynthesisBugWorkaround=*/
       opt.shouldAddVivadoRAMAddressConflictSynthesisBugWorkaround()}));

  // If enabled, run the optimizer.
  if (!opt.shouldDisableOptimization()) {
    auto &modulePM = pm.nest<hw::HWModuleOp>();
    modulePM.addPass(mlir::createCSEPass());
    modulePM.addPass(createSimpleCanonicalizerPass());
    modulePM.addPass(mlir::createCSEPass());
    modulePM.addPass(sv::createHWCleanupPass(
        /*mergeAlwaysBlocks=*/!opt.shouldEmitSeparateAlwaysBlocks()));
  }

  // Check inner symbols and inner refs.
  pm.addPass(hw::createVerifyInnerRefNamespacePass());

  // Check OM object fields.
  pm.addPass(om::createVerifyObjectFieldsPass());

  return success();
}

namespace detail {
LogicalResult
populatePrepareForExportVerilog(mlir::PassManager &pm,
                                const firtool::FirtoolOptions &opt) {

  // Run the verif op verification pass
  pm.addNestedPass<hw::HWModuleOp>(verif::createVerifyClockedAssertLikePass());

  // Legalize unsupported operations within the modules.
  pm.nest<hw::HWModuleOp>().addPass(sv::createHWLegalizeModulesPass());

  // Tidy up the IR to improve verilog emission quality.
  if (!opt.shouldDisableOptimization())
    pm.nest<hw::HWModuleOp>().addPass(sv::createPrettifyVerilogPass());

  if (opt.shouldStripFirDebugInfo())
    pm.addPass(circt::createStripDebugInfoWithPredPass([](mlir::Location loc) {
      if (auto fileLoc = dyn_cast<FileLineColLoc>(loc))
        return fileLoc.getFilename().getValue().ends_with(".fir");
      return false;
    }));

  if (opt.shouldStripDebugInfo())
    pm.addPass(circt::createStripDebugInfoWithPredPass(
        [](mlir::Location loc) { return true; }));

  // Emit module and testbench hierarchy JSON files.
  if (opt.shouldExportModuleHierarchy())
    pm.addPass(sv::createHWExportModuleHierarchyPass());

  // Check inner symbols and inner refs.
  pm.addPass(hw::createVerifyInnerRefNamespacePass());

  // Check OM object fields.
  pm.addPass(om::createVerifyObjectFieldsPass());

  return success();
}
} // namespace detail

LogicalResult
firtool::populateExportVerilog(mlir::PassManager &pm, const FirtoolOptions &opt,
                               std::unique_ptr<llvm::raw_ostream> os) {
  if (failed(::detail::populatePrepareForExportVerilog(pm, opt)))
    return failure();

  pm.addPass(createExportVerilogPass(std::move(os)));
  return success();
}

LogicalResult firtool::populateExportVerilog(mlir::PassManager &pm,
                                             const FirtoolOptions &opt,
                                             llvm::raw_ostream &os) {
  if (failed(::detail::populatePrepareForExportVerilog(pm, opt)))
    return failure();

  pm.addPass(createExportVerilogPass(os));
  return success();
}

LogicalResult firtool::populateExportSplitVerilog(mlir::PassManager &pm,
                                                  const FirtoolOptions &opt,
                                                  llvm::StringRef directory) {
  if (failed(::detail::populatePrepareForExportVerilog(pm, opt)))
    return failure();

  pm.addPass(createExportSplitVerilogPass(directory));
  return success();
}

LogicalResult firtool::populateFinalizeIR(mlir::PassManager &pm,
                                          const FirtoolOptions &opt) {
  pm.addPass(firrtl::createFinalizeIRPass());
  pm.addPass(om::createFreezePathsPass());

  return success();
}

LogicalResult firtool::populateHWToBTOR2(mlir::PassManager &pm,
                                         const FirtoolOptions &opt,
                                         llvm::raw_ostream &os) {
  pm.addNestedPass<hw::HWModuleOp>(circt::createLowerLTLToCorePass());
  if (opt.shouldFormalNormalizeBTOR2Regs())
    pm.addNestedPass<hw::HWModuleOp>(createBTOR2FormalNormalizeRegsPass());
  pm.addNestedPass<hw::HWModuleOp>(circt::verif::createPrepareForFormalPass());
  pm.addNestedPass<hw::HWModuleOp>(circt::hw::createHWAggregateToCombPass());
  pm.addPass(circt::hw::createFlattenModulesPass());
  pm.addPass(circt::createConvertHWToBTOR2Pass(os));
  return success();
}

LogicalResult firtool::populateHWToBTOR2PP(mlir::PassManager &pm,
                                           const FirtoolOptions &opt,
                                           llvm::raw_ostream &os) {
  pm.addNestedPass<hw::HWModuleOp>(circt::createLowerLTLToCorePass());
  if (opt.shouldFormalNormalizeBTOR2Regs())
    pm.addNestedPass<hw::HWModuleOp>(createBTOR2FormalNormalizeRegsPass());
  pm.addNestedPass<hw::HWModuleOp>(circt::verif::createPrepareForFormalPass());
  pm.addNestedPass<hw::HWModuleOp>(circt::hw::createHWAggregateToCombPass());
//   pm.addPass(circt::hw::createFlattenModulesPass());
  pm.addPass(circt::createConvertHWToBTOR2PPPass(os));
  return success();
}

//===----------------------------------------------------------------------===//
// FIRTOOL CommandLine Options
//===----------------------------------------------------------------------===//

namespace {
/// This struct contains command line options that can be used to initialize
/// various bits of a Firtool pipeline. This uses a struct wrapper to avoid the
/// need for global command line options.
struct FirtoolCmdOptions {
  llvm::cl::opt<std::string> outputFilename{
      "o",
      llvm::cl::desc("Output filename, or directory for split output"),
      llvm::cl::value_desc("filename"),
      llvm::cl::init("-"),
  };

  llvm::cl::opt<bool> disableAnnotationsUnknown{
      "disable-annotation-unknown",
      llvm::cl::desc("Ignore unknown annotations when parsing"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> disableAnnotationsClassless{
      "disable-annotation-classless",
      llvm::cl::desc("Ignore annotations without a class when parsing"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> lowerAnnotationsNoRefTypePorts{
      "lower-annotations-no-ref-type-ports",
      llvm::cl::desc(
          "Create real ports instead of ref type ports when resolving "
          "wiring problems inside the LowerAnnotations pass"),
      llvm::cl::init(false), llvm::cl::Hidden};

  llvm::cl::opt<bool> allowAddingPortsOnPublic{
      "allow-adding-ports-on-public-modules",
      llvm::cl::desc("Allow adding ports to public modules"),
      llvm::cl::init(false), llvm::cl::Hidden};

  llvm::cl::opt<bool> probesToSignals{
      "probes-to-signals",
      llvm::cl::desc("Convert probes to non-probe signals"),
      llvm::cl::init(false), llvm::cl::Hidden};

  llvm::cl::opt<circt::firrtl::PreserveAggregate::PreserveMode>
      preserveAggregate{
          "preserve-aggregate",
          llvm::cl::desc("Specify input file format:"),
          llvm::cl::values(
              clEnumValN(circt::firrtl::PreserveAggregate::None, "none",
                         "Preserve no aggregate"),
              clEnumValN(circt::firrtl::PreserveAggregate::OneDimVec, "1d-vec",
                         "Preserve only 1d vectors of ground type"),
              clEnumValN(circt::firrtl::PreserveAggregate::Vec, "vec",
                         "Preserve only vectors"),
              clEnumValN(circt::firrtl::PreserveAggregate::All, "all",
                         "Preserve vectors and bundles")),
          llvm::cl::init(circt::firrtl::PreserveAggregate::None),
      };

  llvm::cl::opt<firrtl::PreserveValues::PreserveMode> preserveMode{
      "preserve-values",
      llvm::cl::desc("Specify the values which can be optimized away"),
      llvm::cl::values(
          clEnumValN(firrtl::PreserveValues::Strip, "strip",
                     "Strip all names. No name is preserved"),
          clEnumValN(firrtl::PreserveValues::None, "none",
                     "Names could be preserved by best-effort unlike `strip`"),
          clEnumValN(firrtl::PreserveValues::Named, "named",
                     "Preserve values with meaningful names"),
          clEnumValN(firrtl::PreserveValues::All, "all",
                     "Preserve all values")),
      llvm::cl::init(firrtl::PreserveValues::None)};

  llvm::cl::opt<bool> enableDebugInfo{
      "g", llvm::cl::desc("Enable the generation of debug information"),
      llvm::cl::init(false)};

  // Build mode options.
  llvm::cl::opt<firtool::FirtoolOptions::BuildMode> buildMode{
      "O", llvm::cl::desc("Controls how much optimization should be performed"),
      llvm::cl::values(clEnumValN(firtool::FirtoolOptions::BuildModeDebug,
                                  "debug",
                                  "Compile with only necessary optimizations"),
                       clEnumValN(firtool::FirtoolOptions::BuildModeRelease,
                                  "release", "Compile with optimizations")),
      llvm::cl::init(firtool::FirtoolOptions::BuildModeDefault)};

  llvm::cl::opt<bool> disableLayerSink{"disable-layer-sink",
                                       llvm::cl::desc("Disable layer sink"),
                                       cl::init(false)};

  llvm::cl::opt<bool> disableOptimization{
      "disable-opt",
      llvm::cl::desc("Disable optimizations"),
  };

  llvm::cl::opt<bool> exportChiselInterface{
      "export-chisel-interface",
      llvm::cl::desc("Generate a Scala Chisel interface to the top level "
                     "module of the firrtl circuit"),
      llvm::cl::init(false)};

  llvm::cl::opt<std::string> chiselInterfaceOutDirectory{
      "chisel-interface-out-dir",
      llvm::cl::desc(
          "The output directory for generated Chisel interface files"),
      llvm::cl::init("")};

  llvm::cl::opt<bool> vbToBV{
      "vb-to-bv",
      llvm::cl::desc("Transform vectors of bundles to bundles of vectors"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> noDedup{
      "no-dedup",
      llvm::cl::desc("Disable deduplication of structurally identical modules"),
      llvm::cl::init(false)};

  llvm::cl::opt<firrtl::CompanionMode> companionMode{
      "grand-central-companion-mode",
      llvm::cl::desc("Specifies the handling of Grand Central companions"),
      ::llvm::cl::values(
          clEnumValN(firrtl::CompanionMode::Bind, "bind",
                     "Lower companion instances to SystemVerilog binds"),
          clEnumValN(firrtl::CompanionMode::Instantiate, "instantiate",
                     "Instantiate companions in the design"),
          clEnumValN(firrtl::CompanionMode::Drop, "drop",
                     "Remove companions from the design")),
      llvm::cl::init(firrtl::CompanionMode::Bind),
      llvm::cl::Hidden,
  };

  llvm::cl::opt<bool> disableAggressiveMergeConnections{
      "disable-aggressive-merge-connections",
      llvm::cl::desc(
          "Disable aggressive merge connections (i.e. merge all field-level "
          "connections into bulk connections)"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> advancedLayerSink{
      "advanced-layer-sink",
      llvm::cl::desc("Sink logic into layer blocks (advanced)"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> lowerMemories{
      "lower-memories",
      llvm::cl::desc("Lower memories to have memories with masks as an "
                     "array with one memory per ground type"),
      llvm::cl::init(false)};

  llvm::cl::opt<std::string> blackBoxRootPath{
      "blackbox-path",
      llvm::cl::desc(
          "Optional path to use as the root of black box annotations"),
      llvm::cl::value_desc("path"),
      llvm::cl::init(""),
  };

  llvm::cl::opt<bool> replSeqMem{
      "repl-seq-mem",
      llvm::cl::desc("Replace the seq mem for macro replacement and emit "
                     "relevant metadata"),
      llvm::cl::init(false)};

  llvm::cl::opt<std::string> replSeqMemFile{
      "repl-seq-mem-file", llvm::cl::desc("File name for seq mem metadata"),
      llvm::cl::init("")};

  llvm::cl::opt<bool> extractTestCode{
      "extract-test-code", llvm::cl::desc("Run the extract test code pass"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> ignoreReadEnableMem{
      "ignore-read-enable-mem",
      llvm::cl::desc("Ignore the read enable signal, instead of "
                     "assigning X on read disable"),
      llvm::cl::init(false)};

  llvm::cl::opt<firtool::FirtoolOptions::RandomKind> disableRandom{
      llvm::cl::desc(
          "Disable random initialization code (may break semantics!)"),
      llvm::cl::values(
          clEnumValN(firtool::FirtoolOptions::RandomKind::Mem,
                     "disable-mem-randomization",
                     "Disable emission of memory randomization code"),
          clEnumValN(firtool::FirtoolOptions::RandomKind::Reg,
                     "disable-reg-randomization",
                     "Disable emission of register randomization code"),
          clEnumValN(firtool::FirtoolOptions::RandomKind::All,
                     "disable-all-randomization",
                     "Disable emission of all randomization code")),
      llvm::cl::init(firtool::FirtoolOptions::RandomKind::None)};

  llvm::cl::opt<std::string> outputAnnotationFilename{
      "output-annotation-file",
      llvm::cl::desc("Optional output annotation file"),
      llvm::cl::CommaSeparated, llvm::cl::value_desc("filename")};

  llvm::cl::opt<bool> enableAnnotationWarning{
      "warn-on-unprocessed-annotations",
      llvm::cl::desc(
          "Warn about annotations that were not removed by lower-to-hw"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> addMuxPragmas{
      "add-mux-pragmas",
      llvm::cl::desc("Annotate mux pragmas for memory array access"),
      llvm::cl::init(false)};

  llvm::cl::opt<firrtl::VerificationFlavor> verificationFlavor{
      "verification-flavor",
      llvm::cl::desc("Specify a verification flavor used in LowerFIRRTLToHW"),
      llvm::cl::values(
          clEnumValN(firrtl::VerificationFlavor::None, "none",
                     "Use the flavor specified by the op"),
          clEnumValN(firrtl::VerificationFlavor::IfElseFatal, "if-else-fatal",
                     "Use Use `if(cond) else $fatal(..)` format"),
          clEnumValN(firrtl::VerificationFlavor::Immediate, "immediate",
                     "Use immediate verif statements"),
          clEnumValN(firrtl::VerificationFlavor::SVA, "sva", "Use SVA")),
      llvm::cl::init(firrtl::VerificationFlavor::None)};

  llvm::cl::opt<bool> emitSeparateAlwaysBlocks{
      "emit-separate-always-blocks",
      llvm::cl::desc(
          "Prevent always blocks from being merged and emit constructs into "
          "separate always blocks whenever possible"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> etcDisableInstanceExtraction{
      "etc-disable-instance-extraction",
      llvm::cl::desc("Disable extracting instances only that feed test code"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> etcDisableRegisterExtraction{
      "etc-disable-register-extraction",
      llvm::cl::desc("Disable extracting registers that only feed test code"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> etcDisableModuleInlining{
      "etc-disable-module-inlining",
      llvm::cl::desc("Disable inlining modules that only feed test code"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> addVivadoRAMAddressConflictSynthesisBugWorkaround{
      "add-vivado-ram-address-conflict-synthesis-bug-workaround",
      llvm::cl::desc(
          "Add a vivado specific SV attribute (* ram_style = "
          "\"distributed\" *) to unpacked array registers as a workaronud "
          "for a vivado synthesis bug that incorrectly modifies "
          "address conflict behavivor of combinational memories"),
      llvm::cl::init(false)};

  //===----------------------------------------------------------------------===
  // External Clock Gate Options
  //===----------------------------------------------------------------------===

  llvm::cl::opt<std::string> ckgModuleName{
      "ckg-name", llvm::cl::desc("Clock gate module name"),
      llvm::cl::init("EICG_wrapper")};

  llvm::cl::opt<std::string> ckgInputName{
      "ckg-input", llvm::cl::desc("Clock gate input port name"),
      llvm::cl::init("in")};

  llvm::cl::opt<std::string> ckgOutputName{
      "ckg-output", llvm::cl::desc("Clock gate output port name"),
      llvm::cl::init("out")};

  llvm::cl::opt<std::string> ckgEnableName{
      "ckg-enable", llvm::cl::desc("Clock gate enable port name"),
      llvm::cl::init("en")};

  llvm::cl::opt<std::string> ckgTestEnableName{
      "ckg-test-enable",
      llvm::cl::desc("Clock gate test enable port name (optional)"),
      llvm::cl::init("test_en")};

  llvm::cl::opt<bool> exportModuleHierarchy{
      "export-module-hierarchy",
      llvm::cl::desc("Export module and instance hierarchy as JSON"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> stripFirDebugInfo{
      "strip-fir-debug-info",
      llvm::cl::desc(
          "Disable source fir locator information in output Verilog"),
      llvm::cl::init(true)};

  llvm::cl::opt<bool> stripDebugInfo{
      "strip-debug-info",
      llvm::cl::desc("Disable source locator information in output Verilog"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> fixupEICGWrapper{
      "fixup-eicg-wrapper",
      llvm::cl::desc("Lower `EICG_wrapper` modules into clock gate intrinsics"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> addCompanionAssume{
      "add-companion-assume",
      llvm::cl::desc("Add companion assumes to assertions"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> selectDefaultInstanceChoice{
      "select-default-for-unspecified-instance-choice",
      llvm::cl::desc(
          "Specialize instance choice to default, if no option selected"),
      llvm::cl::init(false)};

  llvm::cl::opt<bool> formalNormalizeBTOR2Regs{
      "btor2-formal-normalize-regs",
      llvm::cl::desc("Run conservative formal-oriented register normalization "
                     "before BTOR2/BTOR2++ emission"),
      llvm::cl::init(false)};
};
} // namespace

static llvm::ManagedStatic<FirtoolCmdOptions> clOptions;

/// Register a set of useful command-line options that can be used to configure
/// various flags within the MLIRContext. These flags are used when constructing
/// an MLIR context for initialization.
void circt::firtool::registerFirtoolCLOptions() {
  // Make sure that the options struct has been initialized.
  *clOptions;
}

// Initialize the firtool options with defaults supplied by the cl::opts above.
circt::firtool::FirtoolOptions::FirtoolOptions()
    : outputFilename("-"), disableAnnotationsUnknown(false),
      disableAnnotationsClassless(false), lowerAnnotationsNoRefTypePorts(false),
      allowAddingPortsOnPublic(false), probesToSignals(false),
      preserveAggregate(firrtl::PreserveAggregate::None),
      preserveMode(firrtl::PreserveValues::None), enableDebugInfo(false),
      buildMode(BuildModeRelease), disableLayerSink(false),
      disableOptimization(false), exportChiselInterface(false),
      chiselInterfaceOutDirectory(""), vbToBV(false), noDedup(false),
      companionMode(firrtl::CompanionMode::Bind),
      disableAggressiveMergeConnections(false), advancedLayerSink(false),
      lowerMemories(false), blackBoxRootPath(""), replSeqMem(false),
      replSeqMemFile(""), extractTestCode(false), ignoreReadEnableMem(false),
      disableRandom(RandomKind::None), outputAnnotationFilename(""),
      enableAnnotationWarning(false), addMuxPragmas(false),
      verificationFlavor(firrtl::VerificationFlavor::None),
      emitSeparateAlwaysBlocks(false), etcDisableInstanceExtraction(false),
      etcDisableRegisterExtraction(false), etcDisableModuleInlining(false),
      addVivadoRAMAddressConflictSynthesisBugWorkaround(false),
      ckgModuleName("EICG_wrapper"), ckgInputName("in"), ckgOutputName("out"),
      ckgEnableName("en"), ckgTestEnableName("test_en"), ckgInstName("ckg"),
      exportModuleHierarchy(false), stripFirDebugInfo(true),
      stripDebugInfo(false), fixupEICGWrapper(false), addCompanionAssume(false),
      disableCSEinClasses(false), selectDefaultInstanceChoice(false),
      enableModularBTOR2PPMemorySplitting(false),
      formalNormalizeBTOR2Regs(false) {
  if (!clOptions.isConstructed())
    return;
  outputFilename = clOptions->outputFilename;
  disableAnnotationsUnknown = clOptions->disableAnnotationsUnknown;
  disableAnnotationsClassless = clOptions->disableAnnotationsClassless;
  lowerAnnotationsNoRefTypePorts = clOptions->lowerAnnotationsNoRefTypePorts;
  allowAddingPortsOnPublic = clOptions->allowAddingPortsOnPublic;
  probesToSignals = clOptions->probesToSignals;
  preserveAggregate = clOptions->preserveAggregate;
  preserveMode = clOptions->preserveMode;
  enableDebugInfo = clOptions->enableDebugInfo;
  buildMode = clOptions->buildMode;
  disableLayerSink = clOptions->disableLayerSink;
  disableOptimization = clOptions->disableOptimization;
  exportChiselInterface = clOptions->exportChiselInterface;
  chiselInterfaceOutDirectory = clOptions->chiselInterfaceOutDirectory;
  vbToBV = clOptions->vbToBV;
  noDedup = clOptions->noDedup;
  companionMode = clOptions->companionMode;
  disableAggressiveMergeConnections =
      clOptions->disableAggressiveMergeConnections;
  advancedLayerSink = clOptions->advancedLayerSink;
  lowerMemories = clOptions->lowerMemories;
  blackBoxRootPath = clOptions->blackBoxRootPath;
  replSeqMem = clOptions->replSeqMem;
  replSeqMemFile = clOptions->replSeqMemFile;
  extractTestCode = clOptions->extractTestCode;
  ignoreReadEnableMem = clOptions->ignoreReadEnableMem;
  disableRandom = clOptions->disableRandom;
  outputAnnotationFilename = clOptions->outputAnnotationFilename;
  enableAnnotationWarning = clOptions->enableAnnotationWarning;
  addMuxPragmas = clOptions->addMuxPragmas;
  verificationFlavor = clOptions->verificationFlavor;
  emitSeparateAlwaysBlocks = clOptions->emitSeparateAlwaysBlocks;
  etcDisableInstanceExtraction = clOptions->etcDisableInstanceExtraction;
  etcDisableRegisterExtraction = clOptions->etcDisableRegisterExtraction;
  etcDisableModuleInlining = clOptions->etcDisableModuleInlining;
  addVivadoRAMAddressConflictSynthesisBugWorkaround =
      clOptions->addVivadoRAMAddressConflictSynthesisBugWorkaround;
  ckgModuleName = clOptions->ckgModuleName;
  ckgInputName = clOptions->ckgInputName;
  ckgOutputName = clOptions->ckgOutputName;
  ckgEnableName = clOptions->ckgEnableName;
  ckgTestEnableName = clOptions->ckgTestEnableName;
  exportModuleHierarchy = clOptions->exportModuleHierarchy;
  stripFirDebugInfo = clOptions->stripFirDebugInfo;
  stripDebugInfo = clOptions->stripDebugInfo;
  fixupEICGWrapper = clOptions->fixupEICGWrapper;
  addCompanionAssume = clOptions->addCompanionAssume;
  selectDefaultInstanceChoice = clOptions->selectDefaultInstanceChoice;
  formalNormalizeBTOR2Regs = clOptions->formalNormalizeBTOR2Regs;
}

// RUN: firtool %s --format=mlir --btor2 --btor2-formal-normalize-regs -o /dev/null --mlir-disable-threading --mlir-print-ir-after-all 2>&1 | FileCheck %s
// RUN: firtool %s --format=mlir --btor2pp --btor2-formal-normalize-regs -o /dev/null --mlir-disable-threading --mlir-print-ir-after-all 2>&1 | FileCheck %s

module {
  hw.module @FormalNormalizeRegs(
      in %clock: !seq.clock,
      in %reset: i1,
      in %en: i1,
      out selfOut: i1,
      out constHoldOut: i1,
      out updateHoldOut: i1,
      out resetHoldOut: i1,
      out symOut: i1) {
    %false = hw.constant false

    %self = seq.firreg %self clock %clock : i1

    %constHold = seq.firreg %constHoldNext clock %clock : i1
    %constHoldNext = comb.mux bin %en, %false, %constHold : i1

    %updateHold = seq.firreg %updateHoldNext clock %clock : i1
    %updateHoldNext = comb.mux bin %en, %updateHold, %en : i1

    %resetHold = seq.firreg %resetHoldNext clock %clock reset sync %reset, %false : i1
    %resetHoldNext = comb.mux bin %en, %resetHold, %en : i1

    %sym = seq.firreg %sym clock %clock sym @sym : i1

    hw.output %self, %constHold, %updateHold, %resetHold, %sym : i1, i1, i1, i1, i1
  }
}

// CHECK-LABEL: IR Dump After {anonymous}::BTOR2FormalNormalizeRegsPass
// CHECK-LABEL: hw.module @FormalNormalizeRegs
// CHECK: %[[ZERO:.*]] = hw.constant false
// CHECK: %constHold = seq.firreg %{{.*}} clock %clock
// CHECK: comb.and %{{.*}}, %constHold : i1
// CHECK: %updateHold = seq.firreg %{{.*}} clock %clock
// CHECK: %[[TRUE:.*]] = hw.constant true
// CHECK: %[[NOT_EN:.*]] = comb.xor bin %en, %[[TRUE]] : i1
// CHECK: comb.mux bin %[[NOT_EN]], %en, %updateHold : i1
// CHECK: %resetHold = seq.firreg %{{.*}} clock %clock reset sync %reset
// CHECK: comb.mux bin %{{.*}}, %en, %resetHold : i1
// CHECK: %sym = seq.firreg %sym clock %clock sym @sym
// CHECK: hw.output %[[ZERO]], %constHold, %updateHold, %resetHold, %sym : i1, i1, i1, i1, i1

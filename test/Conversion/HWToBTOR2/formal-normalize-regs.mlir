// RUN: circt-opt %s --convert-hw-to-btor2=btor2-formal-normalize-regs=true -o - | FileCheck %s

module {
  hw.module @DirectShadowRegs(
      in %clock: !seq.clock,
      in %en: i1,
      in %update: i1,
      out depOut: i1) {
    %dep = seq.firreg %depNext clock %clock : i1
    %depUpdate = comb.xor bin %dep, %update : i1
    %depNext = comb.mux bin %en, %depUpdate, %dep : i1

    hw.output %dep : i1
  }
}

// CHECK-DAG: [[BIT:[0-9]+]] sort bitvec 1
// CHECK-DAG: [[EN:[0-9]+]] input [[BIT]] en
// CHECK-DAG: [[UPDATE:[0-9]+]] input [[BIT]] update
// CHECK-DAG: [[DEP:[0-9]+]] state [[BIT]] dep
// CHECK: [[DEP_UPDATE:[0-9]+]] xor [[BIT]] [[DEP]] [[UPDATE]]
// CHECK: [[DEP_SHADOW:[0-9]+]] state [[BIT]] __btor2_formal_shadow__dep
// CHECK: [[DEP_EQ:[0-9]+]] eq [[BIT]] [[DEP]] [[DEP_SHADOW]]
// CHECK-NEXT: {{[0-9]+}} constraint [[DEP_EQ]]
// CHECK: [[DEP_NEXT:[0-9]+]] ite [[BIT]] [[EN]] [[DEP_UPDATE]] [[DEP_SHADOW]]
// CHECK: [[DEP_SHADOW_NEXT:[0-9]+]] ite [[BIT]] [[EN]] [[DEP_UPDATE]] [[DEP]]
// CHECK: {{[0-9]+}} next [[BIT]] [[DEP]] [[DEP_NEXT]]
// CHECK: {{[0-9]+}} next [[BIT]] [[DEP_SHADOW]] [[DEP_SHADOW_NEXT]]

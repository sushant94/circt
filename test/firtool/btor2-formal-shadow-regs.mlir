// RUN: firtool %s --format=mlir --btor2 --btor2-formal-normalize-regs -o - | FileCheck %s --check-prefix=BTOR2 --implicit-check-not="output {{.*}}__btor2_formal_shadow__"
// RUN: firtool %s --format=mlir --btor2pp --btor2-formal-normalize-regs -o - | FileCheck %s --check-prefix=BTOR2PP --implicit-check-not="output {{.*}}__btor2_formal_shadow__"

module {
  hw.module @FormalShadowRegs(
      in %clock: !seq.clock,
      in %reset: i1,
      in %en: i1,
      in %update: i1,
      out normalOut: i1,
      out invertedOut: i1,
      out resetOut: i1,
      out depOut: i1,
      out symOut: i1) {
    %false = hw.constant false

    %normal = seq.firreg %normalNext clock %clock : i1
    %normalNext = comb.mux bin %en, %update, %normal : i1

    %inverted = seq.firreg %invertedNext clock %clock : i1
    %invertedNext = comb.mux bin %en, %inverted, %update : i1

    %resetReg = seq.firreg %resetNext clock %clock reset sync %reset, %false : i1
    %resetNext = comb.mux bin %en, %update, %resetReg : i1

    %dep = seq.firreg %depNext clock %clock : i1
    %depUpdate = comb.xor bin %dep, %update : i1
    %depNext = comb.mux bin %en, %depUpdate, %dep : i1

    %sym = seq.firreg %symNext clock %clock sym @sym : i1
    %symNext = comb.mux bin %en, %update, %sym : i1

    hw.output %normal, %inverted, %resetReg, %dep, %sym : i1, i1, i1, i1, i1
  }
}

// BTOR2-DAG: [[BIT:[0-9]+]] sort bitvec 1
// BTOR2-DAG: [[RESET:[0-9]+]] input [[BIT]] reset
// BTOR2-DAG: [[EN:[0-9]+]] input [[BIT]] en
// BTOR2-DAG: [[UPDATE:[0-9]+]] input [[BIT]] update
// BTOR2-DAG: [[NORMAL:[0-9]+]] state [[BIT]] normal
// BTOR2-DAG: [[INVERTED:[0-9]+]] state [[BIT]] inverted
// BTOR2-DAG: [[RESET_REG:[0-9]+]] state [[BIT]] resetReg
// BTOR2-DAG: [[SYM:[0-9]+]] state [[BIT]] sym

// mux(en, update, R): next(R) uses shadow, next(shadow) uses R.
// BTOR2-DAG: [[NORMAL_SHADOW:[0-9]+]] state [[BIT]] __btor2_formal_shadow__normal
// BTOR2: [[NORMAL_EQ:[0-9]+]] eq [[BIT]] [[NORMAL]] [[NORMAL_SHADOW]]
// BTOR2-NEXT: {{[0-9]+}} constraint [[NORMAL_EQ]]
// BTOR2: [[NORMAL_NEXT:[0-9]+]] ite [[BIT]] [[EN]] [[UPDATE]] [[NORMAL_SHADOW]]
// BTOR2-NEXT: [[NORMAL_SHADOW_NEXT:[0-9]+]] ite [[BIT]] [[EN]] [[UPDATE]] [[NORMAL]]
// BTOR2-NEXT: {{[0-9]+}} next [[BIT]] [[NORMAL]] [[NORMAL_NEXT]]
// BTOR2-NEXT: {{[0-9]+}} next [[BIT]] [[NORMAL_SHADOW]] [[NORMAL_SHADOW_NEXT]]

// mux(en, R, update): hold polarity is preserved with the shadow branch.
// BTOR2-DAG: [[INVERTED_SHADOW:[0-9]+]] state [[BIT]] __btor2_formal_shadow__inverted
// BTOR2: [[INVERTED_NEXT:[0-9]+]] ite [[BIT]] {{[0-9]+}} [[UPDATE]] [[INVERTED_SHADOW]]
// BTOR2-NEXT: [[INVERTED_SHADOW_NEXT:[0-9]+]] ite [[BIT]] {{[0-9]+}} [[UPDATE]] [[INVERTED]]
// BTOR2-NEXT: {{[0-9]+}} next [[BIT]] [[INVERTED]] [[INVERTED_NEXT]]
// BTOR2-NEXT: {{[0-9]+}} next [[BIT]] [[INVERTED_SHADOW]] [[INVERTED_SHADOW_NEXT]]

// Reset wrapping applies to both original and shadow next values.
// BTOR2-DAG: [[RESET_SHADOW:[0-9]+]] state [[BIT]] __btor2_formal_shadow__resetReg
// BTOR2: [[RESET_BASE:[0-9]+]] ite [[BIT]] [[EN]] [[UPDATE]] [[RESET_SHADOW]]
// BTOR2-NEXT: [[RESET_SHADOW_BASE:[0-9]+]] ite [[BIT]] [[EN]] [[UPDATE]] [[RESET_REG]]
// BTOR2-NEXT: [[RESET_NEXT:[0-9]+]] ite [[BIT]] [[RESET]] {{[0-9]+}} [[RESET_BASE]]
// BTOR2-NEXT: [[RESET_SHADOW_NEXT:[0-9]+]] ite [[BIT]] [[RESET]] {{[0-9]+}} [[RESET_SHADOW_BASE]]
// BTOR2-NEXT: {{[0-9]+}} next [[BIT]] [[RESET_REG]] [[RESET_NEXT]]
// BTOR2-NEXT: {{[0-9]+}} next [[BIT]] [[RESET_SHADOW]] [[RESET_SHADOW_NEXT]]

// Symbol-sensitive registers are skipped.
// BTOR2-NOT: __btor2_formal_shadow__sym

// BTOR2PP-DAG: [[PBIT:[0-9]+]] sort bitvec 1
// BTOR2PP-DAG: [[PRESET:[0-9]+]] input [[PBIT]] reset
// BTOR2PP-DAG: [[PEN:[0-9]+]] input [[PBIT]] en
// BTOR2PP-DAG: [[PUPDATE:[0-9]+]] input [[PBIT]] update
// BTOR2PP-DAG: [[PNORMAL:[0-9]+]] state [[PBIT]] normal
// BTOR2PP-DAG: [[PINVERTED:[0-9]+]] state [[PBIT]] inverted
// BTOR2PP-DAG: [[PRESET_REG:[0-9]+]] state [[PBIT]] resetReg
// BTOR2PP-DAG: [[PSYM:[0-9]+]] state [[PBIT]] sym

// BTOR2PP-DAG: [[PNORMAL_SHADOW:[0-9]+]] state [[PBIT]] __btor2_formal_shadow__normal
// BTOR2PP: [[PNORMAL_EQ:[0-9]+]] eq [[PBIT]] [[PNORMAL]] [[PNORMAL_SHADOW]]
// BTOR2PP-NEXT: {{[0-9]+}} constraint [[PNORMAL_EQ]]
// BTOR2PP: [[PNORMAL_NEXT:[0-9]+]] ite [[PBIT]] [[PEN]] [[PUPDATE]] [[PNORMAL_SHADOW]]
// BTOR2PP-NEXT: [[PNORMAL_SHADOW_NEXT:[0-9]+]] ite [[PBIT]] [[PEN]] [[PUPDATE]] [[PNORMAL]]
// BTOR2PP-NEXT: {{[0-9]+}} next [[PBIT]] [[PNORMAL]] [[PNORMAL_NEXT]]
// BTOR2PP-NEXT: {{[0-9]+}} next [[PBIT]] [[PNORMAL_SHADOW]] [[PNORMAL_SHADOW_NEXT]]

// BTOR2PP-DAG: [[PINVERTED_SHADOW:[0-9]+]] state [[PBIT]] __btor2_formal_shadow__inverted
// BTOR2PP: [[PINVERTED_NEXT:[0-9]+]] ite [[PBIT]] {{[0-9]+}} [[PUPDATE]] [[PINVERTED_SHADOW]]
// BTOR2PP-NEXT: [[PINVERTED_SHADOW_NEXT:[0-9]+]] ite [[PBIT]] {{[0-9]+}} [[PUPDATE]] [[PINVERTED]]
// BTOR2PP-NEXT: {{[0-9]+}} next [[PBIT]] [[PINVERTED]] [[PINVERTED_NEXT]]
// BTOR2PP-NEXT: {{[0-9]+}} next [[PBIT]] [[PINVERTED_SHADOW]] [[PINVERTED_SHADOW_NEXT]]

// BTOR2PP-DAG: [[PRESET_SHADOW:[0-9]+]] state [[PBIT]] __btor2_formal_shadow__resetReg
// BTOR2PP: [[PRESET_BASE:[0-9]+]] ite [[PBIT]] [[PEN]] [[PUPDATE]] [[PRESET_SHADOW]]
// BTOR2PP-NEXT: [[PRESET_SHADOW_BASE:[0-9]+]] ite [[PBIT]] [[PEN]] [[PUPDATE]] [[PRESET_REG]]
// BTOR2PP-NEXT: [[PRESET_NEXT:[0-9]+]] ite [[PBIT]] [[PRESET]] {{[0-9]+}} [[PRESET_BASE]]
// BTOR2PP-NEXT: [[PRESET_SHADOW_NEXT:[0-9]+]] ite [[PBIT]] [[PRESET]] {{[0-9]+}} [[PRESET_SHADOW_BASE]]
// BTOR2PP-NEXT: {{[0-9]+}} next [[PBIT]] [[PRESET_REG]] [[PRESET_NEXT]]
// BTOR2PP-NEXT: {{[0-9]+}} next [[PBIT]] [[PRESET_SHADOW]] [[PRESET_SHADOW_NEXT]]
// BTOR2PP-NOT: __btor2_formal_shadow__sym
// BTOR2PP-NOT: output {{[0-9]+}} __btor2_formal_shadow__

// RUN: circt-opt %s --convert-hw-to-btor2pp -o %t | FileCheck %s

module {
  // CHECK-LABEL: ; ==== Module 'concat_replicate' ====
  // CHECK: [[BIT1:[0-9]+]] sort bitvec 1
  // CHECK: [[A:[0-9]+]] input [[BIT1]] a
  // CHECK: [[B:[0-9]+]] input [[BIT1]] b
  // CHECK: [[C:[0-9]+]] input [[BIT1]] c
  // CHECK: [[BIT2:[0-9]+]] sort bitvec 2
  // CHECK: [[X:[0-9]+]] input [[BIT2]] x
  // CHECK: [[BIT3:[0-9]+]] sort bitvec 3
  // CHECK: [[CAT01:[0-9]+]] concat [[BIT2]] [[A]] [[B]]
  // CHECK: [[CAT012:[0-9]+]] concat [[BIT3]] [[CAT01]] [[C]]
  // CHECK: [[BIT6:[0-9]+]] sort bitvec 6
  // CHECK: [[BIT4:[0-9]+]] sort bitvec 4
  // CHECK: [[REP01:[0-9]+]] concat [[BIT4]] [[X]] [[X]]
  // CHECK: [[REP012:[0-9]+]] concat [[BIT6]] [[REP01]] [[X]]
  hw.module @concat_replicate(in %a : i1, in %b : i1, in %c : i1, in %x : i2,
                              out cat : i3, out rep : i6) {
    %cat = comb.concat %a, %b, %c : i1, i1, i1
    %rep = comb.replicate %x : (i2) -> i6
    hw.output %cat, %rep : i3, i6
  }
}

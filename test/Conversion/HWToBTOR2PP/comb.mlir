// RUN: circt-opt %s --convert-hw-to-btor2pp -o %t | FileCheck %s

module {
  // CHECK-LABEL: ; ==== Module 'inc' ====
  // CHECK: module inc {
  // CHECK: [[NID0:[0-9]+]] sort bitvec 32
  // CHECK: [[NID1:[0-9]+]] input [[NID0]] a
  // CHECK: [[NID2:[0-9]+]] sort bitvec 1
  // CHECK: [[NID3:[0-9]+]] ugt [[NID2]]
  // CHECK: [[NID4:[0-9]+]] bad [[NID5:[0-9]+]]
  // CHECK: [[NID6:[0-9]+]] output [[NID3]] pred
  hw.module @inc(in %a : i32, in %clk : !seq.clock, out pred : i1) {
    %0 = seq.from_clock %clk
    %false = hw.constant false
    %true = hw.constant true
    %1 = comb.concat %false, %a : i1, i32
    %c1_i33 = hw.constant 1 : i33
    %2 = comb.add bin %1, %c1_i33 : i33
    %3 = comb.extract %2 from 0 : (i33) -> i32
    %4 = comb.icmp bin ugt %3, %a : i32
    sv.always posedge %0 {
      sv.if %true {
        sv.assert %4, immediate message "a + 1 should be greater than a"
      }
    }
    hw.output %4 : i1
  }
}

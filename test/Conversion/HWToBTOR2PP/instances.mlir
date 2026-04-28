// RUN: circt-opt %s --convert-hw-to-btor2pp -o %t | FileCheck %s

module {
  // CHECK-LABEL: ; ==== Module 'Leaf' ====
  // CHECK: module Leaf {
  // CHECK: [[LEAF_SORT:[0-9]+]] sort bitvec 1
  // CHECK: [[LEAF_IN:[0-9]+]] input [[LEAF_SORT]] in
  // CHECK: [[LEAF_OUT:[0-9]+]] output [[LEAF_IN]] out
  hw.module @Leaf(in %in : i1, out out : i1) {
    hw.output %in : i1
  }

  // CHECK-LABEL: ; ==== Module 'Top' ====
  // CHECK: include Leaf.btor2pp
  // CHECK: module Top {
  // CHECK: [[TOP_SORT:[0-9]+]] sort bitvec 1
  // CHECK: [[TOP_A:[0-9]+]] input [[TOP_SORT]] a
  // CHECK: [[TOP_B:[0-9]+]] input [[TOP_SORT]] b
  // CHECK: [[INST0:[0-9]+]] inst Leaf alpha
  // CHECK: [[REF_IN:[0-9]+]] ref Leaf [[LEAF_IN]] in
  // CHECK: [[REF_OUT:[0-9]+]] ref Leaf [[LEAF_OUT]] out
  // CHECK: [[GET0:[0-9]+]] get [[INST0]] [[REF_OUT]] out
  // CHECK: [[INST1:[0-9]+]] inst Leaf beta
  // CHECK: [[GET1:[0-9]+]] get [[INST1]] [[REF_OUT]] out
  // CHECK: [[SET0:[0-9]+]] set [[INST0]] [[REF_IN]] [[TOP_A]] in
  // CHECK: [[SET1:[0-9]+]] set [[INST1]] [[REF_IN]] [[TOP_B]] in
  // CHECK: [[TOP_OUT0:[0-9]+]] output [[GET0]] out0
  // CHECK: [[TOP_OUT1:[0-9]+]] output [[GET1]] out1
  hw.module @Top(in %a : i1, in %b : i1, out out0 : i1, out out1 : i1) {
    %alpha.out = hw.instance "alpha" @Leaf(in: %a: i1) -> (out: i1)
    %beta.out = hw.instance "beta" @Leaf(in: %b: i1) -> (out: i1)
    hw.output %alpha.out, %beta.out : i1, i1
  }
}

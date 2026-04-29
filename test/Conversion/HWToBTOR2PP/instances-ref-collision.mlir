// RUN: circt-opt %s --convert-hw-to-btor2pp -o %t | FileCheck %s

module {
  // CHECK-LABEL: ; ==== Module 'Alpha' ====
  // CHECK: module Alpha {
  // CHECK: [[ALPHA_SORT:[0-9]+]] sort bitvec 1
  // CHECK: [[ALPHA_IN:[0-9]+]] input [[ALPHA_SORT]] in
  // CHECK: [[ALPHA_OUT:[0-9]+]] output [[ALPHA_IN]] out
  hw.module @Alpha(in %in : i1, out out : i1) {
    hw.output %in : i1
  }

  // CHECK-LABEL: ; ==== Module 'Beta' ====
  // CHECK: module Beta {
  // CHECK: [[BETA_IN_SORT:[0-9]+]] sort bitvec 1
  // CHECK: [[BETA_IN:[0-9]+]] input [[BETA_IN_SORT]] in
  // CHECK: [[BETA_OUT_SORT:[0-9]+]] sort bitvec 2
  // CHECK: [[BETA_OUT:[0-9]+]] output {{[0-9]+}} out
  hw.module @Beta(in %in : i1, out out : i2) {
    %0 = comb.concat %in, %in : i1, i1
    hw.output %0 : i2
  }

  // CHECK-LABEL: ; ==== Module 'Top' ====
  // CHECK: include Alpha.btor2pp
  // CHECK: include Beta.btor2pp
  // CHECK: module Top {
  // CHECK: [[TOP_SORT:[0-9]+]] sort bitvec 1
  // CHECK: [[TOP_A:[0-9]+]] input [[TOP_SORT]] a
  // CHECK: [[TOP_B:[0-9]+]] input [[TOP_SORT]] b
  // CHECK: [[TOP_C:[0-9]+]] input [[TOP_SORT]] c
  // CHECK: [[ALPHA0:[0-9]+]] inst Alpha alpha0
  // CHECK: [[ALPHA_REF_IN:[0-9]+]] ref Alpha [[ALPHA_IN]] in
  // CHECK: [[ALPHA_REF_OUT:[0-9]+]] ref Alpha [[ALPHA_OUT]] out
  // CHECK: [[GET_ALPHA0:[0-9]+]] get [[ALPHA0]] [[ALPHA_REF_OUT]] out
  // CHECK: [[BETA0:[0-9]+]] inst Beta beta0
  // CHECK: [[BETA_REF_IN:[0-9]+]] ref Beta [[BETA_IN]] in
  // CHECK: [[BETA_REF_OUT:[0-9]+]] ref Beta [[BETA_OUT]] out
  // CHECK: [[GET_BETA0:[0-9]+]] get [[BETA0]] [[BETA_REF_OUT]] out
  // CHECK: [[ALPHA1:[0-9]+]] inst Alpha alpha1
  // CHECK: [[GET_ALPHA1:[0-9]+]] get [[ALPHA1]] [[ALPHA_REF_OUT]] out
  // CHECK: [[SET_ALPHA0:[0-9]+]] set [[ALPHA0]] [[ALPHA_REF_IN]] [[TOP_A]] in
  // CHECK: [[SET_BETA0:[0-9]+]] set [[BETA0]] [[BETA_REF_IN]] [[TOP_B]] in
  // CHECK: [[SET_ALPHA1:[0-9]+]] set [[ALPHA1]] [[ALPHA_REF_IN]] [[TOP_C]] in
  hw.module @Top(in %a : i1, in %b : i1, in %c : i1,
                 out out0 : i1, out out1 : i2, out out2 : i1) {
    %alpha0.out = hw.instance "alpha0" @Alpha(in: %a : i1) -> (out: i1)
    %beta0.out = hw.instance "beta0" @Beta(in: %b : i1) -> (out: i2)
    %alpha1.out = hw.instance "alpha1" @Alpha(in: %c : i1) -> (out: i1)
    hw.output %alpha0.out, %beta0.out, %alpha1.out : i1, i2, i1
  }
}

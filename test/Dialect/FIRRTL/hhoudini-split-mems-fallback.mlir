// RUN: circt-opt -pass-pipeline='builtin.module(firrtl.circuit(firrtl-hhoudini-split-mems,firrtl-mem-to-reg-of-vec-fallback))' %s | FileCheck %s

firrtl.circuit "SplitAndFallback" attributes {annotations = [{class = "circt.HHoudiniSplitMemsAnnotation"}]} {
  // CHECK-LABEL: firrtl.circuit "SplitAndFallback"
  // CHECK: firrtl.module private @good_hhoudini_row
  // CHECK: firrtl.module private @good_hhoudini_wrap
  // CHECK: %good_r, %good_w = firrtl.instance good @good_hhoudini_wrap
  // CHECK: %bad = firrtl.reg
  // CHECK-NOT: firrtl.mem
  firrtl.module public @SplitAndFallback() {
    %clk = firrtl.wire : !firrtl.clock
    %good_r, %good_w = firrtl.mem Undefined {depth = 2 : i64, name = "good", portNames = ["r", "w"], readLatency = 0 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>, !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %0 = firrtl.subfield %good_r[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %1 = firrtl.subfield %good_r[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %2 = firrtl.subfield %good_r[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %3 = firrtl.subfield %good_w[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %4 = firrtl.subfield %good_w[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %5 = firrtl.subfield %good_w[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %6 = firrtl.subfield %good_w[data] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %7 = firrtl.subfield %good_w[mask] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %bad_r, %bad_w = firrtl.mem Undefined {depth = 2 : i64, name = "bad", portNames = ["r", "w"], readLatency = 0 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: sint<8>>, !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %8 = firrtl.subfield %bad_r[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: sint<8>>
    %9 = firrtl.subfield %bad_r[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: sint<8>>
    %10 = firrtl.subfield %bad_r[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: sint<8>>
    %11 = firrtl.subfield %bad_w[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %12 = firrtl.subfield %bad_w[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %13 = firrtl.subfield %bad_w[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %14 = firrtl.subfield %bad_w[data] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %15 = firrtl.subfield %bad_w[mask] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %c0 = firrtl.constant 0 : !firrtl.uint<1>
    %c1 = firrtl.constant 1 : !firrtl.uint<1>
    %c8 = firrtl.constant 8 : !firrtl.uint<8>
    %cs = firrtl.constant 1 : !firrtl.sint<8>
    firrtl.connect %0, %c0 : !firrtl.uint<1>
    firrtl.connect %1, %c1 : !firrtl.uint<1>
    firrtl.connect %2, %clk : !firrtl.clock
    firrtl.connect %3, %c0 : !firrtl.uint<1>
    firrtl.connect %4, %c1 : !firrtl.uint<1>
    firrtl.connect %5, %clk : !firrtl.clock
    firrtl.connect %6, %c8 : !firrtl.uint<8>
    firrtl.connect %7, %c1 : !firrtl.uint<1>
    firrtl.connect %8, %c0 : !firrtl.uint<1>
    firrtl.connect %9, %c1 : !firrtl.uint<1>
    firrtl.connect %10, %clk : !firrtl.clock
    firrtl.connect %11, %c0 : !firrtl.uint<1>
    firrtl.connect %12, %c1 : !firrtl.uint<1>
    firrtl.connect %13, %clk : !firrtl.clock
    firrtl.connect %14, %cs : !firrtl.sint<8>
    firrtl.connect %15, %c1 : !firrtl.uint<1>
  }
}

firrtl.circuit "SyncSplitAndFallback" attributes {annotations = [{class = "circt.HHoudiniSplitMemsAnnotation"}]} {
  // CHECK-LABEL: firrtl.circuit "SyncSplitAndFallback"
  // CHECK: firrtl.module private @good_hhoudini_row
  // CHECK: firrtl.module private @good_hhoudini_wrap
  // CHECK: %good_r0, %good_r1, %good_w0, %good_w1 = firrtl.instance good @good_hhoudini_wrap
  // CHECK: %bad = firrtl.reg
  // CHECK-NOT: firrtl.mem
  firrtl.module public @SyncSplitAndFallback() {
    %clk = firrtl.wire : !firrtl.clock
    %good_r0, %good_r1, %good_w0, %good_w1 = firrtl.mem Old {depth = 2 : i64, name = "good", portNames = ["r0", "r1", "w0", "w1"], readLatency = 1 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>, !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>, !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>, !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %0 = firrtl.subfield %good_r0[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %1 = firrtl.subfield %good_r0[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %2 = firrtl.subfield %good_r0[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %3 = firrtl.subfield %good_r1[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %4 = firrtl.subfield %good_r1[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %5 = firrtl.subfield %good_r1[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %6 = firrtl.subfield %good_w0[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %7 = firrtl.subfield %good_w0[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %8 = firrtl.subfield %good_w0[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %9 = firrtl.subfield %good_w0[data] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %10 = firrtl.subfield %good_w0[mask] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %11 = firrtl.subfield %good_w1[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %12 = firrtl.subfield %good_w1[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %13 = firrtl.subfield %good_w1[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %14 = firrtl.subfield %good_w1[data] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %15 = firrtl.subfield %good_w1[mask] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %bad_r, %bad_w = firrtl.mem Old {depth = 2 : i64, name = "bad", portNames = ["r", "w"], readLatency = 1 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: sint<8>>, !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %16 = firrtl.subfield %bad_r[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: sint<8>>
    %17 = firrtl.subfield %bad_r[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: sint<8>>
    %18 = firrtl.subfield %bad_r[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: sint<8>>
    %19 = firrtl.subfield %bad_w[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %20 = firrtl.subfield %bad_w[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %21 = firrtl.subfield %bad_w[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %22 = firrtl.subfield %bad_w[data] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %23 = firrtl.subfield %bad_w[mask] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: sint<8>, mask: uint<1>>
    %c0 = firrtl.constant 0 : !firrtl.uint<1>
    %c1 = firrtl.constant 1 : !firrtl.uint<1>
    %c8 = firrtl.constant 8 : !firrtl.uint<8>
    %c9 = firrtl.constant 9 : !firrtl.uint<8>
    %cs = firrtl.constant 1 : !firrtl.sint<8>
    firrtl.connect %0, %c0 : !firrtl.uint<1>
    firrtl.connect %1, %c1 : !firrtl.uint<1>
    firrtl.connect %2, %clk : !firrtl.clock
    firrtl.connect %3, %c1 : !firrtl.uint<1>
    firrtl.connect %4, %c1 : !firrtl.uint<1>
    firrtl.connect %5, %clk : !firrtl.clock
    firrtl.connect %6, %c0 : !firrtl.uint<1>
    firrtl.connect %7, %c1 : !firrtl.uint<1>
    firrtl.connect %8, %clk : !firrtl.clock
    firrtl.connect %9, %c8 : !firrtl.uint<8>
    firrtl.connect %10, %c1 : !firrtl.uint<1>
    firrtl.connect %11, %c1 : !firrtl.uint<1>
    firrtl.connect %12, %c1 : !firrtl.uint<1>
    firrtl.connect %13, %clk : !firrtl.clock
    firrtl.connect %14, %c9 : !firrtl.uint<8>
    firrtl.connect %15, %c1 : !firrtl.uint<1>
    firrtl.connect %16, %c0 : !firrtl.uint<1>
    firrtl.connect %17, %c1 : !firrtl.uint<1>
    firrtl.connect %18, %clk : !firrtl.clock
    firrtl.connect %19, %c0 : !firrtl.uint<1>
    firrtl.connect %20, %c1 : !firrtl.uint<1>
    firrtl.connect %21, %clk : !firrtl.clock
    firrtl.connect %22, %cs : !firrtl.sint<8>
    firrtl.connect %23, %c1 : !firrtl.uint<1>
  }
}

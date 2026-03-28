// RUN: circt-opt -pass-pipeline='builtin.module(firrtl.circuit(firrtl-hhoudini-split-mems))' %s | FileCheck %s

firrtl.circuit "NoAnnotation" {
  // CHECK-LABEL: firrtl.circuit "NoAnnotation"
  // CHECK-NOT: hhoudini
  firrtl.module public @NoAnnotation() {
    %clk = firrtl.wire : !firrtl.clock
    %m_r, %m_w = firrtl.mem Undefined {depth = 2 : i64, name = "m", portNames = ["r", "w"], readLatency = 0 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>, !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %0 = firrtl.subfield %m_r[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %1 = firrtl.subfield %m_r[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %2 = firrtl.subfield %m_r[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %3 = firrtl.subfield %m_w[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %4 = firrtl.subfield %m_w[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %5 = firrtl.subfield %m_w[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %6 = firrtl.subfield %m_w[data] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %7 = firrtl.subfield %m_w[mask] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %c0 = firrtl.constant 0 : !firrtl.uint<1>
    %c1 = firrtl.constant 1 : !firrtl.uint<1>
    %c8 = firrtl.constant 8 : !firrtl.uint<8>
    firrtl.connect %0, %c0 : !firrtl.uint<1>
    firrtl.connect %1, %c1 : !firrtl.uint<1>
    firrtl.connect %2, %clk : !firrtl.clock
    firrtl.connect %3, %c0 : !firrtl.uint<1>
    firrtl.connect %4, %c1 : !firrtl.uint<1>
    firrtl.connect %5, %clk : !firrtl.clock
    firrtl.connect %6, %c8 : !firrtl.uint<8>
    firrtl.connect %7, %c1 : !firrtl.uint<1>
  }
}

firrtl.circuit "AsyncSupported" attributes {annotations = [{class = "circt.HHoudiniSplitMemsAnnotation"}]} {
  // CHECK-LABEL: firrtl.circuit "AsyncSupported"
  // CHECK: firrtl.module private @mem_hhoudini_row
  // CHECK: firrtl.module private @mem_hhoudini_wrap
  // CHECK: firrtl.instance row_0 @mem_hhoudini_row
  // CHECK: firrtl.instance row_1 @mem_hhoudini_row
  // CHECK: %mem_r, %mem_w = firrtl.instance mem @mem_hhoudini_wrap
  firrtl.module public @AsyncSupported() {
    %clk = firrtl.wire : !firrtl.clock
    %mem_r, %mem_w = firrtl.mem Undefined {depth = 2 : i64, name = "mem", portNames = ["r", "w"], readLatency = 0 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>, !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %0 = firrtl.subfield %mem_r[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %1 = firrtl.subfield %mem_r[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %2 = firrtl.subfield %mem_r[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %3 = firrtl.subfield %mem_w[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %4 = firrtl.subfield %mem_w[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %5 = firrtl.subfield %mem_w[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %6 = firrtl.subfield %mem_w[data] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %7 = firrtl.subfield %mem_w[mask] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %c0 = firrtl.constant 0 : !firrtl.uint<1>
    %c1 = firrtl.constant 1 : !firrtl.uint<1>
    %c8 = firrtl.constant 8 : !firrtl.uint<8>
    firrtl.connect %0, %c0 : !firrtl.uint<1>
    firrtl.connect %1, %c1 : !firrtl.uint<1>
    firrtl.connect %2, %clk : !firrtl.clock
    firrtl.connect %3, %c0 : !firrtl.uint<1>
    firrtl.connect %4, %c1 : !firrtl.uint<1>
    firrtl.connect %5, %clk : !firrtl.clock
    firrtl.connect %6, %c8 : !firrtl.uint<8>
    firrtl.connect %7, %c1 : !firrtl.uint<1>
  }
}

firrtl.circuit "SyncSupported" attributes {annotations = [{class = "circt.HHoudiniSplitMemsAnnotation"}]} {
  // CHECK-LABEL: firrtl.circuit "SyncSupported"
  // CHECK: firrtl.module private @mem_hhoudini_row
  // CHECK: firrtl.module private @mem_hhoudini_wrap
  // CHECK: %read_en_q_0 = firrtl.reg
  // CHECK: %read_data_q_0 = firrtl.reg
  // CHECK: %mem_r, %mem_w = firrtl.instance mem @mem_hhoudini_wrap
  firrtl.module public @SyncSupported() {
    %clk = firrtl.wire : !firrtl.clock
    %mem_r, %mem_w = firrtl.mem Old {depth = 2 : i64, name = "mem", portNames = ["r", "w"], readLatency = 1 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>, !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %0 = firrtl.subfield %mem_r[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %1 = firrtl.subfield %mem_r[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %2 = firrtl.subfield %mem_r[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: uint<8>>
    %3 = firrtl.subfield %mem_w[addr] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %4 = firrtl.subfield %mem_w[en] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %5 = firrtl.subfield %mem_w[clk] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %6 = firrtl.subfield %mem_w[data] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %7 = firrtl.subfield %mem_w[mask] : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %c0 = firrtl.constant 0 : !firrtl.uint<1>
    %c1 = firrtl.constant 1 : !firrtl.uint<1>
    %c8 = firrtl.constant 8 : !firrtl.uint<8>
    firrtl.connect %0, %c0 : !firrtl.uint<1>
    firrtl.connect %1, %c1 : !firrtl.uint<1>
    firrtl.connect %2, %clk : !firrtl.clock
    firrtl.connect %3, %c0 : !firrtl.uint<1>
    firrtl.connect %4, %c1 : !firrtl.uint<1>
    firrtl.connect %5, %clk : !firrtl.clock
    firrtl.connect %6, %c8 : !firrtl.uint<8>
    firrtl.connect %7, %c1 : !firrtl.uint<1>
  }
}

firrtl.circuit "MultiPortSyncSupported" attributes {annotations = [{class = "circt.HHoudiniSplitMemsAnnotation"}]} {
  // CHECK-LABEL: firrtl.circuit "MultiPortSyncSupported"
  // CHECK-DAG: firrtl.module private @mem_hhoudini_row
  // CHECK-DAG: firrtl.module private @mem_hhoudini_wrap
  // CHECK-DAG: firrtl.instance row_0 @mem_hhoudini_row
  // CHECK-DAG: firrtl.instance row_3 @mem_hhoudini_row
  // CHECK-DAG: %read_en_q_0 = firrtl.reg
  // CHECK-DAG: %read_data_q_0 = firrtl.reg
  // CHECK-DAG: %read_en_q_1 = firrtl.reg
  // CHECK-DAG: %read_data_q_1 = firrtl.reg
  // CHECK-DAG: firrtl.or
  // CHECK: %mem_r0, %mem_r1, %mem_w0, %mem_w1 = firrtl.instance mem @mem_hhoudini_wrap
  firrtl.module public @MultiPortSyncSupported() {
    %clk = firrtl.wire : !firrtl.clock
    %mem_r0, %mem_r1, %mem_w0, %mem_w1 = firrtl.mem Old {depth = 4 : i64, name = "mem", portNames = ["r0", "r1", "w0", "w1"], readLatency = 1 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>, !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>, !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>, !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %0 = firrtl.subfield %mem_r0[addr] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %1 = firrtl.subfield %mem_r0[en] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %2 = firrtl.subfield %mem_r0[clk] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %3 = firrtl.subfield %mem_r1[addr] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %4 = firrtl.subfield %mem_r1[en] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %5 = firrtl.subfield %mem_r1[clk] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %6 = firrtl.subfield %mem_w0[addr] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %7 = firrtl.subfield %mem_w0[en] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %8 = firrtl.subfield %mem_w0[clk] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %9 = firrtl.subfield %mem_w0[data] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %10 = firrtl.subfield %mem_w0[mask] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %11 = firrtl.subfield %mem_w1[addr] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %12 = firrtl.subfield %mem_w1[en] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %13 = firrtl.subfield %mem_w1[clk] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %14 = firrtl.subfield %mem_w1[data] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %15 = firrtl.subfield %mem_w1[mask] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %c0 = firrtl.constant 0 : !firrtl.uint<2>
    %c1 = firrtl.constant 1 : !firrtl.uint<1>
    %c1_2 = firrtl.constant 1 : !firrtl.uint<2>
    %c2 = firrtl.constant 2 : !firrtl.uint<2>
    %c8 = firrtl.constant 8 : !firrtl.uint<8>
    %c9 = firrtl.constant 9 : !firrtl.uint<8>
    firrtl.connect %0, %c0 : !firrtl.uint<2>
    firrtl.connect %1, %c1 : !firrtl.uint<1>
    firrtl.connect %2, %clk : !firrtl.clock
    firrtl.connect %3, %c1_2 : !firrtl.uint<2>
    firrtl.connect %4, %c1 : !firrtl.uint<1>
    firrtl.connect %5, %clk : !firrtl.clock
    firrtl.connect %6, %c1_2 : !firrtl.uint<2>
    firrtl.connect %7, %c1 : !firrtl.uint<1>
    firrtl.connect %8, %clk : !firrtl.clock
    firrtl.connect %9, %c8 : !firrtl.uint<8>
    firrtl.connect %10, %c1 : !firrtl.uint<1>
    firrtl.connect %11, %c2 : !firrtl.uint<2>
    firrtl.connect %12, %c1 : !firrtl.uint<1>
    firrtl.connect %13, %clk : !firrtl.clock
    firrtl.connect %14, %c9 : !firrtl.uint<8>
    firrtl.connect %15, %c1 : !firrtl.uint<1>
  }
}

firrtl.circuit "MultiPortAsyncSupported" attributes {annotations = [{class = "circt.HHoudiniSplitMemsAnnotation"}]} {
  // CHECK-LABEL: firrtl.circuit "MultiPortAsyncSupported"
  // CHECK: firrtl.module private @mem_hhoudini_row
  // CHECK: firrtl.module private @mem_hhoudini_wrap
  // CHECK: firrtl.instance row_0 @mem_hhoudini_row
  // CHECK: firrtl.instance row_3 @mem_hhoudini_row
  // CHECK: firrtl.or
  // CHECK: %mem_r0, %mem_r1, %mem_w0, %mem_w1 = firrtl.instance mem @mem_hhoudini_wrap
  firrtl.module public @MultiPortAsyncSupported() {
    %clk = firrtl.wire : !firrtl.clock
    %mem_r0, %mem_r1, %mem_w0, %mem_w1 = firrtl.mem Undefined {depth = 4 : i64, name = "mem", portNames = ["r0", "r1", "w0", "w1"], readLatency = 0 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>, !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>, !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>, !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %0 = firrtl.subfield %mem_r0[addr] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %1 = firrtl.subfield %mem_r0[en] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %2 = firrtl.subfield %mem_r0[clk] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %3 = firrtl.subfield %mem_r1[addr] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %4 = firrtl.subfield %mem_r1[en] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %5 = firrtl.subfield %mem_r1[clk] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data flip: uint<8>>
    %6 = firrtl.subfield %mem_w0[addr] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %7 = firrtl.subfield %mem_w0[en] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %8 = firrtl.subfield %mem_w0[clk] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %9 = firrtl.subfield %mem_w0[data] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %10 = firrtl.subfield %mem_w0[mask] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %11 = firrtl.subfield %mem_w1[addr] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %12 = firrtl.subfield %mem_w1[en] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %13 = firrtl.subfield %mem_w1[clk] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %14 = firrtl.subfield %mem_w1[data] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %15 = firrtl.subfield %mem_w1[mask] : !firrtl.bundle<addr: uint<2>, en: uint<1>, clk: clock, data: uint<8>, mask: uint<1>>
    %c0 = firrtl.constant 0 : !firrtl.uint<2>
    %c1 = firrtl.constant 1 : !firrtl.uint<1>
    %c1_2 = firrtl.constant 1 : !firrtl.uint<2>
    %c2 = firrtl.constant 2 : !firrtl.uint<2>
    %c8 = firrtl.constant 8 : !firrtl.uint<8>
    %c9 = firrtl.constant 9 : !firrtl.uint<8>
    firrtl.connect %0, %c0 : !firrtl.uint<2>
    firrtl.connect %1, %c1 : !firrtl.uint<1>
    firrtl.connect %2, %clk : !firrtl.clock
    firrtl.connect %3, %c1_2 : !firrtl.uint<2>
    firrtl.connect %4, %c1 : !firrtl.uint<1>
    firrtl.connect %5, %clk : !firrtl.clock
    firrtl.connect %6, %c1_2 : !firrtl.uint<2>
    firrtl.connect %7, %c1 : !firrtl.uint<1>
    firrtl.connect %8, %clk : !firrtl.clock
    firrtl.connect %9, %c8 : !firrtl.uint<8>
    firrtl.connect %10, %c1 : !firrtl.uint<1>
    firrtl.connect %11, %c2 : !firrtl.uint<2>
    firrtl.connect %12, %c1 : !firrtl.uint<1>
    firrtl.connect %13, %clk : !firrtl.clock
    firrtl.connect %14, %c9 : !firrtl.uint<8>
    firrtl.connect %15, %c1 : !firrtl.uint<1>
  }
}

firrtl.circuit "MixedSupport" attributes {annotations = [{class = "circt.HHoudiniSplitMemsAnnotation"}]} {
  // CHECK-LABEL: firrtl.circuit "MixedSupport"
  // CHECK: firrtl.module private @good_hhoudini_row
  // CHECK: firrtl.module private @good_hhoudini_wrap
  // CHECK-NOT: @bad_hhoudini_wrap
  // CHECK: %bad_r, %bad_w = firrtl.mem Undefined
  firrtl.module public @MixedSupport() {
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

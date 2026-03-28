// RUN: circt-opt -pass-pipeline='builtin.module(firrtl.circuit(firrtl-hhoudini-split-mems))' %s >/dev/null

firrtl.circuit "NeedsFlattenMemory" attributes {annotations = [{class = "circt.HHoudiniSplitMemsAnnotation"}]} {
  firrtl.module public @NeedsFlattenMemory() {
    %mem_r, %mem_w = firrtl.mem Undefined {depth = 2 : i64, name = "mem", portNames = ["r", "w"], readLatency = 0 : i32, writeLatency = 1 : i32} : !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data flip: bundle<a: uint<8>>>, !firrtl.bundle<addr: uint<1>, en: uint<1>, clk: clock, data: bundle<a: uint<8>>, mask: bundle<a: uint<1>>>
  }
}

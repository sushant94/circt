; RUN: firtool %s --format=fir --btor2pp -o /dev/null --mlir-disable-threading --mlir-print-ir-after=firrtl-hhoudini-split-mems 2>&1 | FileCheck %s

FIRRTL version 4.0.0
circuit Core: %[[{"class": "circt.HHoudiniSplitMemsAnnotation"}]]
  public module Core:
    input clock: Clock
    input raddr0: UInt<5>
    input raddr1: UInt<5>
    input waddr0: UInt<5>
    input waddr1: UInt<5>
    input wen0: UInt<1>
    input wen1: UInt<1>
    input wdata0: UInt<32>
    input wdata1: UInt<32>
    output rdata0: UInt<32>
    output rdata1: UInt<32>

    cmem regfile : UInt<32> [32]
    infer mport r0 = regfile[raddr0], clock
    infer mport r1 = regfile[raddr1], clock
    when wen0 :
      infer mport w0 = regfile[waddr0], clock
      connect w0, wdata0
    when wen1 :
      infer mport w1 = regfile[waddr1], clock
      connect w1, wdata1
    connect rdata0, r0
    connect rdata1, r1

; CHECK: IR Dump After HHoudiniSplitMems
; CHECK: firrtl.module private @regfile_hhoudini_row
; CHECK: firrtl.module private @regfile_hhoudini_wrap
; CHECK: firrtl.instance row_0 @regfile_hhoudini_row
; CHECK: firrtl.instance row_31 @regfile_hhoudini_row
; CHECK: %regfile_r0, %regfile_r1, %regfile_w0, %regfile_w1 = firrtl.instance regfile @regfile_hhoudini_wrap

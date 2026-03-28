; RUN: firtool %s --format=fir --btor2pp -o /dev/null --mlir-disable-threading --mlir-print-ir-after=firrtl-hhoudini-split-mems 2>&1 | FileCheck %s

FIRRTL version 4.0.0
circuit Core: %[[{"class": "circt.HHoudiniSplitMemsAnnotation"}]]
  public module Core:
    input clock: Clock
    input raddr0: UInt<5>
    input raddr1: UInt<5>
    input waddr0: UInt<5>
    input waddr1: UInt<5>
    input ren0: UInt<1>
    input ren1: UInt<1>
    input wen0: UInt<1>
    input wen1: UInt<1>
    input wdata0: UInt<32>
    input wdata1: UInt<32>
    output rdata0: UInt<32>
    output rdata1: UInt<32>

    mem regfile:
      data-type => UInt<32>
      depth => 32
      reader => r0
      reader => r1
      writer => w0
      writer => w1
      read-latency => 1
      write-latency => 1
      read-under-write => old

    connect regfile.r0.clk, clock
    connect regfile.r0.en, ren0
    connect regfile.r0.addr, raddr0
    connect regfile.r1.clk, clock
    connect regfile.r1.en, ren1
    connect regfile.r1.addr, raddr1
    connect rdata0, regfile.r0.data
    connect rdata1, regfile.r1.data

    connect regfile.w0.clk, clock
    connect regfile.w0.en, wen0
    connect regfile.w0.addr, waddr0
    connect regfile.w0.mask, UInt<1>(1)
    connect regfile.w0.data, wdata0

    connect regfile.w1.clk, clock
    connect regfile.w1.en, wen1
    connect regfile.w1.addr, waddr1
    connect regfile.w1.mask, UInt<1>(1)
    connect regfile.w1.data, wdata1

; CHECK: IR Dump After HHoudiniSplitMems
; CHECK: firrtl.module private @regfile_hhoudini_row
; CHECK: firrtl.module private @regfile_hhoudini_wrap
; CHECK: firrtl.instance row_0 @regfile_hhoudini_row
; CHECK: firrtl.instance row_31 @regfile_hhoudini_row
; CHECK: %read_en_q_0 = firrtl.reg
; CHECK: %read_data_q_0 = firrtl.reg
; CHECK: %read_en_q_1 = firrtl.reg
; CHECK: %read_data_q_1 = firrtl.reg
; CHECK: %regfile_r0, %regfile_r1, %regfile_w0, %regfile_w1 = firrtl.instance regfile @regfile_hhoudini_wrap

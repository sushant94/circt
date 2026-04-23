; RUN: firtool %s --format=fir --btor2pp -o /dev/null --mlir-disable-threading --mlir-print-ir-after=firrtl-hhoudini-split-reg-vecs 2>&1 | FileCheck %s --implicit-check-not=firrtl.mem

FIRRTL version 4.0.0
circuit Top: %[[{
    "class": "circt.HHoudiniSplitRegVecsAnnotation"
  },
  {
    "class": "sifive.enterprise.firrtl.ConvertMemToRegOfVecAnnotation$"
  }]]
  public module Top:
    input clock: Clock
    input addr: UInt<2>
    input en: UInt<1>
    input data: UInt<8>
    output out: UInt<8>

    mem mem:
      data-type => UInt<8>
      depth => 4
      reader => r
      writer => w
      read-latency => 0
      write-latency => 1
      read-under-write => undefined

    connect mem.r.clk, clock
    connect mem.r.en, en
    connect mem.r.addr, addr
    connect out, mem.r.data
    connect mem.w.clk, clock
    connect mem.w.en, en
    connect mem.w.addr, addr
    connect mem.w.mask, UInt<1>(1)
    connect mem.w.data, data

; CHECK: IR Dump After HHoudiniSplitRegVecs
; CHECK: firrtl.module private @mem_hhoudini_row
; CHECK: firrtl.module private @mem_hhoudini_wrap
; CHECK: firrtl.instance row_0 @mem_hhoudini_row
; CHECK: firrtl.instance row_3 @mem_hhoudini_row
; CHECK: firrtl.module @Top
; CHECK: %mem_clk, %mem_d, %mem_q = firrtl.instance mem @mem_hhoudini_wrap
; CHECK: firrtl.matchingconnect %mem_d, %mem_q

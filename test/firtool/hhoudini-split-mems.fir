; RUN: firtool %s --format=fir --btor2pp -o /dev/null --mlir-disable-threading --mlir-print-ir-after=firrtl-hhoudini-split-mems 2>&1 | FileCheck %s --check-prefix=BTOR2PP
; RUN: not firtool %s --format=fir --btor2 -o /dev/null --mlir-disable-threading --mlir-print-ir-after=firrtl-hhoudini-split-mems 2>&1 | FileCheck %s --check-prefix=BTOR2 --implicit-check-not="IR Dump After HHoudiniSplitMems"

FIRRTL version 4.0.0
circuit Top: %[[{"class": "circt.HHoudiniSplitMemsAnnotation"}]]
  public module Top:
    input clock: Clock
    input addr: UInt<1>
    input en: UInt<1>
    input data: UInt<8>
    output out: UInt<8>

    mem mem:
      data-type => UInt<8>
      depth => 2
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

; BTOR2PP: IR Dump After HHoudiniSplitMems
; BTOR2PP: firrtl.module private @mem_hhoudini_row
; BTOR2PP: firrtl.module private @mem_hhoudini_wrap
; BTOR2PP: %mem_r, %mem_w = firrtl.instance mem @mem_hhoudini_wrap

; BTOR2: error: 'seq.firmem' op is an unsupported operation

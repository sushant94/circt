; RUN: firtool %s --format=fir --btor2pp -o /dev/null --mlir-disable-threading --mlir-print-ir-after=firrtl-mem-to-reg-of-vec-fallback 2>&1 | FileCheck %s

FIRRTL version 4.0.0
circuit Top: %[[{"class": "circt.HHoudiniSplitMemsAnnotation"}]]
  public module Top:
    input clock: Clock
    input addr: UInt<1>
    input en: UInt<1>
    input data: UInt<8>
    output out: UInt<8>

    mem good:
      data-type => UInt<8>
      depth => 2
      reader => r
      writer => w
      read-latency => 0
      write-latency => 1
      read-under-write => undefined

    mem bad:
      data-type => SInt<8>
      depth => 2
      reader => r
      writer => w
      read-latency => 0
      write-latency => 1
      read-under-write => undefined

    connect good.r.clk, clock
    connect good.r.en, en
    connect good.r.addr, addr
    connect out, good.r.data
    connect good.w.clk, clock
    connect good.w.en, en
    connect good.w.addr, addr
    connect good.w.mask, UInt<1>(1)
    connect good.w.data, data

    connect bad.r.clk, clock
    connect bad.r.en, en
    connect bad.r.addr, addr
    connect bad.w.clk, clock
    connect bad.w.en, en
    connect bad.w.addr, addr
    connect bad.w.mask, UInt<1>(1)
    connect bad.w.data, asSInt(data)

; CHECK: IR Dump After MemToRegOfVecFallback
; CHECK: firrtl.module private @good_hhoudini_row
; CHECK: firrtl.module private @good_hhoudini_wrap
; CHECK: %good_r, %good_w = firrtl.instance good @good_hhoudini_wrap
; CHECK: %bad = firrtl.reg
; CHECK-NOT: firrtl.mem

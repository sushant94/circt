; RUN: firtool %s --format=fir --btor2pp -o /dev/null --mlir-disable-threading --mlir-print-ir-after=firrtl-hhoudini-split-reg-vecs 2>&1 | FileCheck %s --implicit-check-not=scalar_hhoudini --implicit-check-not=bundle_hhoudini --implicit-check-not=one_hhoudini --implicit-check-not=annotated_hhoudini --implicit-check-not=mem_hhoudini

FIRRTL version 4.0.0
circuit Top: %[[{
    "class": "circt.HHoudiniSplitRegVecsAnnotation"
  },
  {
    "class": "firrtl.transforms.DontTouchAnnotation",
    "target": "~Top|Top>annotated"
  }]]
  public module Top:
    input clock: Clock
    input reset: UInt<1>
    input en: UInt<1>
    input in: UInt<8>[4]
    input init: UInt<8>[2]
    input bundleIn: { a: UInt<4>, b: UInt<4> }[2]
    input nestedIn: UInt<2>[3][2]
    output out: UInt<8>[4]
    output outReset: UInt<8>[2]
    output bundleOut: { a: UInt<4>, b: UInt<4> }[2]
    output nestedOut: UInt<2>[3][2]
    output scalarOut: UInt<8>
    output plainBundleOut: { a: UInt<8>, b: UInt<8> }
    output oneOut: UInt<8>[1]
    output memOut: UInt<8>
    output annotatedOut: UInt<8>[2]

    reg r: UInt<8>[4], clock
    when en:
      connect r, in
    connect out, r

    regreset rr: UInt<8>[2], clock, reset, init
    connect outReset, rr

    reg vecBundle: { a: UInt<4>, b: UInt<4> }[2], clock
    connect vecBundle, bundleIn
    connect bundleOut, vecBundle

    reg nestedVec: UInt<2>[3][2], clock
    connect nestedVec, nestedIn
    connect nestedOut, nestedVec

    reg scalar: UInt<8>, clock
    connect scalar, UInt<8>(0)
    connect scalarOut, scalar

    reg bundle: { a: UInt<8>, b: UInt<8> }, clock
    connect bundle.a, UInt<8>(1)
    connect bundle.b, UInt<8>(2)
    connect plainBundleOut, bundle

    reg one: UInt<8>[1], clock
    connect one[0], UInt<8>(3)
    connect oneOut, one

    wire annotatedNext: UInt<8>[2]
    connect annotatedNext[0], UInt<8>(4)
    connect annotatedNext[1], UInt<8>(5)
    reg annotated: UInt<8>[2], clock
    connect annotated, annotatedNext
    connect annotatedOut, annotated

    mem mem:
      data-type => UInt<8>
      depth => 2
      reader => r0
      writer => w0
      read-latency => 0
      write-latency => 1
      read-under-write => undefined
    connect mem.r0.clk, clock
    connect mem.r0.en, UInt<1>(1)
    connect mem.r0.addr, UInt<1>(0)
    connect memOut, mem.r0.data
    connect mem.w0.clk, clock
    connect mem.w0.en, UInt<1>(0)
    connect mem.w0.addr, UInt<1>(0)
    connect mem.w0.mask, UInt<1>(1)
    connect mem.w0.data, UInt<8>(0)

; CHECK: IR Dump After HHoudiniSplitRegVecs
; CHECK: firrtl.module private @r_hhoudini_row
; CHECK: firrtl.module private @r_hhoudini_wrap
; CHECK: firrtl.instance row_0 @r_hhoudini_row
; CHECK: firrtl.instance row_3 @r_hhoudini_row

; CHECK: firrtl.module private @rr_hhoudini_row
; CHECK-SAME: in %reset
; CHECK-SAME: in %reset_value
; CHECK: firrtl.regreset %clk, %reset, %reset_value
; CHECK: firrtl.module private @rr_hhoudini_wrap
; CHECK: firrtl.subindex %reset_value[0]
; CHECK: firrtl.subindex %reset_value[1]

; CHECK: firrtl.module private @vecBundle_hhoudini_row
; CHECK-SAME: !firrtl.bundle<a: uint<4>, b: uint<4>>
; CHECK: firrtl.module private @nestedVec_hhoudini_row
; CHECK-SAME: !firrtl.vector<uint<2>, 3>

; CHECK: firrtl.module @Top
; CHECK: %r_clk, %r_d, %r_q = firrtl.instance r @r_hhoudini_wrap
; CHECK: firrtl.matchingconnect %r_d, %r_q
; CHECK: firrtl.when %en
; CHECK: firrtl.matchingconnect %r_d, %in
; CHECK: firrtl.matchingconnect %out, %r_q
; CHECK: %rr_clk, %rr_reset, %rr_reset_value, %rr_d, %rr_q = firrtl.instance rr @rr_hhoudini_wrap
; CHECK: firrtl.matchingconnect %rr_reset, %reset
; CHECK: firrtl.matchingconnect %rr_reset_value, %init
; CHECK: firrtl.matchingconnect %rr_d, %rr_q
; CHECK: firrtl.matchingconnect %outReset, %rr_q
; CHECK: firrtl.mem

; RUN: firtool %s --format=fir --ir-fir --low-firrtl-pass-plugin='firrtl.circuit(firrtl-btor2pp-lower-plusargs)' | FileCheck %s --check-prefix=FIR
; RUN: firtool %s --format=fir --ir-fir --low-firrtl-pass-plugin='firrtl.circuit(firrtl-btor2-lower-plusargs)' | FileCheck %s --check-prefix=FIR
; RUN: firtool %s --format=fir --btor2pp | FileCheck %s --check-prefix=BTOR2PP
; RUN: firtool %s --format=fir --ir-hw | FileCheck %s --check-prefix=HW
FIRRTL version 4.0.0

circuit Top:
  extmodule plusarg_reader :
    output out : UInt<8>
    defname = plusarg_reader
    parameter DEFAULT = 3
    parameter FORMAT = "max_core_cycles=%d"
    parameter WIDTH = 8

  module PlusArgTimeout :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip count : UInt<8> }

    inst plusarg_reader of plusarg_reader
    node enabled = gt(plusarg_reader.out, UInt<1>(0h0))
    when enabled :
      node ok = lt(io.count, plusarg_reader.out)
      assert(clock, ok, UInt<1>(0h1), "")

  public module Top :
    input clock : Clock
    input reset : UInt<1>

    wire count : UInt<8>
    connect count, UInt<8>(0h2)

    inst timeout of PlusArgTimeout
    connect timeout.clock, clock
    connect timeout.reset, reset
    connect timeout.io.count, count

; FIR-LABEL: firrtl.module private @PlusArgTimeout
; FIR-NOT: plusarg_reader
; FIR: %c3_ui8 = firrtl.constant 3 : !firrtl.uint<8>
; FIR: %enabled = firrtl.gt %c3_ui8, %c0_ui1
; FIR: %ok = firrtl.gt %c3_ui8, %c2_ui8
; FIR-NOT: firrtl.extmodule

; BTOR2PP-LABEL: module Top {
; BTOR2PP-NOT: plusarg_reader

; HW: hw.module.extern private @plusarg_reader
; HW: %plusarg_reader.out = hw.instance "plusarg_reader" @plusarg_reader
; HW: hw.instance "timeout" @PlusArgTimeout

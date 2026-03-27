; RUN: firtool %s --format=fir --ir-fir --low-firrtl-pass-plugin='firrtl.circuit(firrtl-btor2-lower-plusargs)' | FileCheck %s --check-prefix=FIR
; RUN: firtool %s --format=fir --btor2 | FileCheck %s --check-prefix=BTOR2
FIRRTL version 4.0.0

circuit Top:
  extmodule plusarg_reader :
    output out : UInt<8>
    defname = plusarg_reader
    parameter DEFAULT = 3
    parameter FORMAT = "max_core_cycles=%d"
    parameter WIDTH = 8

  public module Top :
    output out : UInt<8>

    inst plusarg_reader of plusarg_reader
    connect out, plusarg_reader.out

; FIR-LABEL: firrtl.module @Top
; FIR-NOT: plusarg_reader
; FIR: %c3_ui8 = firrtl.constant 3 : !firrtl.uint<8>
; FIR: firrtl.matchingconnect %out, %c3_ui8 : !firrtl.uint<8>

; BTOR2-NOT: plusarg_reader
; BTOR2: 1 sort bitvec 8
; BTOR2: 2 consth 1 3
; BTOR2: 3 output 2 out

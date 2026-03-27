; RUN: not firtool %s --format=fir --ir-fir --low-firrtl-pass-plugin='firrtl.circuit(firrtl-btor2-lower-plusargs)' 2>&1 | FileCheck %s
; RUN: not firtool %s --format=fir --btor2 2>&1 | FileCheck %s
FIRRTL version 4.0.0

circuit Top:
  extmodule plusarg_reader :
    output out : UInt<8>
    defname = plusarg_reader
    parameter FORMAT = "max_core_cycles=%d"
    parameter WIDTH = 8

  public module Top :
    output out : UInt<8>

    inst plusarg_reader of plusarg_reader
    connect out, plusarg_reader.out

; CHECK: error: 'firrtl.extmodule' op expected plusarg_reader extmodule to define DEFAULT parameter

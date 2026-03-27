; RUN: not firtool %s --format=fir --btor2 2>&1 | FileCheck %s
FIRRTL version 4.0.0

circuit Top:
  public module Top :
    input clock : Clock
    input reset : UInt<1>

; CHECK: error: the flat --btor2 exporter is not yet restored; use --btor2pp for the current modular BTOR2++ flow

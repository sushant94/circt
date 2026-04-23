// RUN: circt-opt --firrtl-hhoudini-split-reg-vecs %s | FileCheck %s --implicit-check-not=sym_hhoudini --implicit-check-not=force_hhoudini --implicit-check-not=annotated_hhoudini --implicit-check-not=zero_hhoudini

firrtl.circuit "SkipUnsupported" attributes {
  annotations = [{class = "circt.HHoudiniSplitRegVecsAnnotation"}]} {
  firrtl.module @SkipUnsupported(
      in %clock: !firrtl.clock,
      in %in: !firrtl.vector<uint<8>, 2>,
      out %validOut: !firrtl.vector<uint<8>, 2>,
      out %symOut: !firrtl.vector<uint<8>, 2>,
      out %forceOut: !firrtl.vector<uint<8>, 2>,
      out %annotatedOut: !firrtl.vector<uint<8>, 2>) {
    %valid = firrtl.reg %clock {name = "valid"} : !firrtl.clock, !firrtl.vector<uint<8>, 2>
    firrtl.matchingconnect %valid, %in : !firrtl.vector<uint<8>, 2>
    firrtl.matchingconnect %validOut, %valid : !firrtl.vector<uint<8>, 2>

    %sym = firrtl.reg sym @sym %clock {name = "sym"} : !firrtl.clock, !firrtl.vector<uint<8>, 2>
    firrtl.matchingconnect %sym, %in : !firrtl.vector<uint<8>, 2>
    firrtl.matchingconnect %symOut, %sym : !firrtl.vector<uint<8>, 2>

    %force, %force_ref = firrtl.reg %clock forceable {name = "force"} : !firrtl.clock, !firrtl.vector<uint<8>, 2>, !firrtl.rwprobe<vector<uint<8>, 2>>
    firrtl.matchingconnect %force, %in : !firrtl.vector<uint<8>, 2>
    firrtl.matchingconnect %forceOut, %force : !firrtl.vector<uint<8>, 2>

    %annotated = firrtl.reg %clock {annotations = [{class = "firrtl.transforms.DontTouchAnnotation"}], name = "annotated"} : !firrtl.clock, !firrtl.vector<uint<8>, 2>
    firrtl.matchingconnect %annotated, %in : !firrtl.vector<uint<8>, 2>
    firrtl.matchingconnect %annotatedOut, %annotated : !firrtl.vector<uint<8>, 2>

    %zero = firrtl.reg %clock {name = "zero"} : !firrtl.clock, !firrtl.vector<uint<0>, 2>
  }
}

// CHECK: firrtl.module private @valid_hhoudini_row
// CHECK: firrtl.module private @valid_hhoudini_wrap
// CHECK: %valid_clk, %valid_d, %valid_q = firrtl.instance valid @valid_hhoudini_wrap
// CHECK: firrtl.reg sym @sym
// CHECK: firrtl.reg %clock forceable
// CHECK: firrtl.reg %clock {annotations = [{class = "firrtl.transforms.DontTouchAnnotation"}]}
// CHECK: firrtl.reg %clock : !firrtl.clock, !firrtl.vector<uint<0>, 2>

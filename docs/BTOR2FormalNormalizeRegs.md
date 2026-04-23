# BTOR2 Formal Register Normalization

`firtool` supports an opt-in BTOR2/BTOR2++ export flag:

```sh
firtool input.fir --btor2 --btor2-formal-normalize-regs -o output.btor2
```

For BTOR2++:

```sh
firtool input.fir --btor2pp --btor2-formal-normalize-regs -o output.btor2pp
```

The flag is off by default. It only affects the formal BTOR2 and BTOR2++
pipelines and does not rewrite the source hardware based on invariant or proof
knowledge.

## What It Does

The flag enables two conservative, semantics-preserving normalizations:

- trivial register cleanup before export, including existing FIRRTL/Seq-safe
  self-loop and constant folding cases;
- BTOR2/BTOR2++ shadow-state encoding for eligible enabled self-hold registers.

For a direct self-hold update:

```mlir
%next = comb.mux bin %en, %update, %reg : i1
%reg = seq.firreg %next clock %clock : i1
```

the exporter emits standard BTOR2 along these lines:

```text
state R
state __btor2_formal_shadow__R
eq R __btor2_formal_shadow__R
constraint <eq-result>
next R                        = ite(en, update, __btor2_formal_shadow__R)
next __btor2_formal_shadow__R = ite(en, update, R)
```

For reset-wrapped registers, reset is applied to both next expressions:

```text
next R      = ite(reset, resetValue, ite(en, update, shadow))
next shadow = ite(reset, resetValue, ite(en, update, R))
constraint R == shadow
```

The constraint keeps the legal traces equivalent to the original self-hold
transition while making `next(R)` directly reference the shadow state instead
of `R`. This is intended to make state cones friendlier for downstream
inductive-proof tooling that treats direct self-dependencies specially.

## Shadow State Contract

Shadow states use the reserved prefix:

```text
__btor2_formal_shadow__
```

Formal consumers should treat these as internal export artifacts. In
particular, pairmap mining, `EqPred` generation, `EqPredConst` generation, and
invariant reporting should filter any state whose name starts with that prefix.

The exporter does not emit BTOR2 `output` lines for shadow states. The name
prefix is still the primary machine-readable skip signal.

Consumers must honor standard BTOR2 `constraint` semantics. Ignoring
constraints makes the shadow encoding over-approximate the original transition
system.

## Eligibility

The shadow encoding applies only to direct self-hold mux shapes:

```text
mux(en, update, R)
mux(en, R, update)
```

The exporter does not reject a register merely because `update` depends on `R`.
That dependency is valid under the shadow constraint `R == shadow`.

The exporter skips cases that are structurally unsupported or preservation
sensitive:

- both mux branches are the register, or neither branch is the register;
- symbol-sensitive, forceable, annotated, or otherwise protected registers;
- unsupported aggregate or non-bitvector state types;
- cases where the exporter cannot assign stable BTOR2 line IDs.

## Direct Conversion

For direct `circt-opt` testing of the conversion passes:

```sh
circt-opt input.mlir --convert-hw-to-btor2=btor2-formal-normalize-regs=true
circt-opt input.mlir --convert-hw-to-btor2pp=btor2-formal-normalize-regs=true
```

## Statistics

The BTOR2 and BTOR2++ conversion passes report statistics for:

- emitted shadow states;
- reset-wrapped shadow encodings;
- protected register skips;
- unsupported mux-shape skips;
- unsupported state-type skips.

## Equivalence Check

The shadow encoding relies on the invariant that the original and shadow state
are equal on legal traces. A one-step check can be expressed as:

```smt2
(set-logic QF_BV)
(declare-const r (_ BitVec 1))
(declare-const shadow (_ BitVec 1))
(declare-const en (_ BitVec 1))
(declare-const update (_ BitVec 1))
(assert (= r shadow))
(define-fun original_next () (_ BitVec 1)
  (ite (= en #b1) update r))
(define-fun shadow_encoded_next () (_ BitVec 1)
  (ite (= en #b1) update shadow))
(assert (not (= original_next shadow_encoded_next)))
(check-sat)
```

The expected answer is `unsat`. The reset-wrapped version is analogous, with
both expressions wrapped by the same reset mux.

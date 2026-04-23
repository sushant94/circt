# Synthesis Next Steps for CIRCT BTOR2 Invariants

This note captures follow-up work for the external invariant synthesis flow.
The CIRCT change that introduced `--btor2-formal-normalize-regs` is intentionally
compiler-local: it emits cleaner BTOR2/BTOR2++ for formal consumers, but it does
not run synthesis experiments and does not change the synthesis algorithm.

When that flag is enabled, CIRCT may emit constrained internal shadow states
whose names start with `__btor2_formal_shadow__`. The synthesis miner should
filter those states from pairmap mining, `EqPred` generation, `EqPredConst`
generation, and invariant reporting.

## Recommended Algorithm Work

Synthesis should detect `EqPredConst` sub-task failures where the state cone of
influence contains only the predicate register itself. These cases often come
from self-hold register updates such as `next(x) = mux(enable, update, x)`.
They may be globally provable only when broader pipeline and safety predicates
are available.

For these detected failures, retry the predicate with a bounded parent-frame
hypothesis set. The frame should be small enough to preserve decomposition, but
large enough to include the context that made the parent proof inductive:
parent active predicates, predicates present in the parent UNSAT core, and
safety/context predicates such as `IsSafeInstrPred`.

Cache rescued predicates distinctly from ordinary local-COI proofs. This makes
it clear which predicates depended on widened context and prevents the same
child from repeatedly triggering delete/resynthesize loops in
`cascade_mode="retry"`.

## Evaluation Matrix

Compare Rocket and BOOM across these configurations:

- baseline CIRCT BTOR2 with `cascade_mode="retry"`;
- CIRCT BTOR2 with `--btor2-formal-normalize-regs` only;
- synthesis self-COI rescue only;
- both CIRCT normalization and synthesis rescue enabled.

Track total predicates proved, self-COI `EqPredConst` failures, rescue attempts,
successful rescues, repeated retry cascades avoided, and final proof validity.

## Boundary

CIRCT should not use invariant knowledge or proof results to rewrite hardware.
Compiler-side normalization must remain semantics-preserving. Predicate rescue,
broader-frame retries, and retry-cascade policy belong to the synthesis flow.

# DenseBins mishandles builds with no bins

## Severity

Medium

## Affected code

The integer-bin GPU, OpenMP, and serial `DenseBins::build` overloads in
`Src/Particle/AMReX_DenseBins.H` (and their `Box` forwarding overloads).

## Explanation

The three policies disagree when `nbins <= 0`:

- OpenMP returns immediately without clearing an earlier build, leaving stale
  items, offsets, and permutations observable.
- GPU and serial proceed with a one-element count/offset vector. If `nitems` is
  nonzero, the mapper's bin index is used against storage for zero bins,
  producing out-of-bounds accesses.
- An empty `Box` forwards its point count into these same paths, so the hazard
  is reachable through the documented 3D overload too.

There is no documented positive-bin precondition, and a reused container
should not retain an unrelated prior build after being rebuilt empty.

## Proposed patch

Give all policies one shared entry check. For `nbins <= 0`, require
`nitems == 0`, clear all per-build vectors, retain a defined empty offsets
representation, update `m_items`, and return. For `nitems > 0`, fail with a
descriptive always-on assertion or error before launching work.

Add policy-parameterized tests covering an initially empty build, rebuilding a
populated container with zero items/zero bins, and rejecting nonzero items with
zero bins.


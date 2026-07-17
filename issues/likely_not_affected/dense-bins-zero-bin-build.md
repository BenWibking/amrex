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

## Verification

**Conclusion: Confirmed.**

The GPU and serial builders allocate `nbins + 1` offsets and then index the
count array with every mapper result. With `nbins == 0` and nonempty input,
that is immediately out of bounds. The OpenMP branch instead returns without
resetting the object, so rebuilding an existing instance with zero bins also
leaves stale state. Empty `Box` construction is a normal route to zero bins.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka exercises `DenseBins` indirectly through AMReX particle location and
redistribution, but every active Quokka AMR level has a nonempty level-zero
domain and a nonempty `BoxArray`. Locator construction therefore supplies a
positive bin count; an empty particle population still means zero items in a
positive grid/bin structure, not a nonempty build with zero bins. Quokka does
not directly call the public integer-bin build overload with a zero `nbins`
value or reuse a `DenseBins` object for an empty `Box`.

The affected class is nevertheless part of Quokka's ordinary locator stack,
and levels are created and destroyed dynamically during regridding. A future
empty-level or custom-binning path could violate the current positive-grid
invariant. Present hierarchy construction avoids the trigger, which supports
likely not affected without claiming that the dependency can never expose it.

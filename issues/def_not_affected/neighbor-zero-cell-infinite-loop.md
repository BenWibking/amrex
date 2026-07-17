# Zero neighbor-cell width causes an infinite loop

## Severity

High

## Affected code

The neighbor-container constructors and mask path of
`NeighborParticleContainer_impl::getNeighborTags` in
`Src/Particle/AMReX_NeighborParticlesI.H`.

## Explanation

The constructors accept `ncells`/`nneighbor` without validation. In the
single-level mask path, tag generation walks offsets with loops of the form:

```cpp
for (int ii = -nGrow[0]; ii < nGrow[0] + 1; ii += nGrow[0])
```

When the configured neighbor-cell count is zero, the loop starts at zero, its
condition remains true, and the increment is zero. `fillNeighbors()` hangs in
an infinite loop. Negative values can also be passed into mask allocation as a
grow width.

## Proposed patch

Define the supported contract explicitly. If a neighbor container requires a
positive halo, reject nonpositive values with an always-on constructor check.
If zero is intended to mean no neighbor halo, short-circuit tag/mask creation
without entering the stepped loops. Validate every directional `IntVect`
component before using it as a loop stride.

Add tests for zero, negative, and one-cell configurations.

## Verification

**Conclusion: Confirmed.**

Neighbor constructors store the signed width without validation. In the
single-level mask path, each offset loop increments by the corresponding
`nGrow` component. A zero component therefore produces a loop whose condition
remains true and whose induction variable never changes.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The nonterminating loop is in `NeighborParticleContainer_impl` mask creation,
not the base particle container's ordinary `Redistribute` handling of
`nGrow == 0`. Quokka never constructs a neighbor container and therefore never
stores a neighbor-cell width or calls `getNeighborTags`. Quokka routinely uses
zero grow values in standard redistribution, but that code has different loop
logic and is not implicated by this report.

The precise affected type is absent, so Quokka cannot enter the zero-step
offset loop. Adding a neighbor container would be required, making current
non-impact definite.

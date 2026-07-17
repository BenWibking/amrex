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

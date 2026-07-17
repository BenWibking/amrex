# Cross-container neighbor lists misclassify target ghosts

## Severity

Medium

## Affected code

The two passes of the general `NeighborList::build(SrcTile&, TargetTile&,
...)` overload in `Src/Particle/AMReX_NeighborList.H`.

## Explanation

Candidate neighbor indices (`pid`) come from bins built over `target_tile`, so
the real/ghost boundary for those indices is
`target_tile.numRealParticles()`. The implementation instead stores only
`np_real = src_tile.numRealParticles()` and computes:

```cpp
bool ghost_pid = (pid >= np_real);
```

When source and target containers have different real-particle counts in the
same tile, `ghost_pid` is wrong. The error is passed to the most detailed
`check_pair` callback overload in both the counting and filling passes. A
callback that treats real and ghost targets differently can therefore reject
valid pairs or accept duplicates, even though the actual target index and data
are otherwise correct.

## Proposed patch

Store separate `src_np_real` and `dst_np_real` counts. Use the source count for
classifying `i` and determining the number of source queries, and the target
count for classifying each binned `pid`.

Add a cross-container test whose source and target tiles have unequal real and
neighbor counts. Use the six-argument callback to verify `ghost_i` and
`ghost_pid` for every returned pair.

## Verification

**Conclusion: Confirmed.**

The cross-tile builder bins `target_tile` particles but stores only
`src_tile.numRealParticles()`. Both passes classify a target `pid` by comparing
it with that source count. Unequal source and target real counts therefore
produce incorrect `ghost_pid` arguments to the detailed callback.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This logic is exclusive to the general cross-container `NeighborList::build`
overload. Quokka does not build AMReX neighbor lists or compare source and
target particle tiles through this callback. Its different particle species
deposit independently to meshes and are combined at the mesh/registry level,
so no target PID is classified against another tile's real-particle count.

The affected cross-container algorithm is wholly absent from Quokka's call
graph. It would require a new particle-particle neighbor feature, so the
classification is definite.

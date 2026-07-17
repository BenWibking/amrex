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

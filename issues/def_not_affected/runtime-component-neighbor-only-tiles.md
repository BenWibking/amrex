# Runtime component changes skip neighbor-only particle tiles

## Severity

High

## Affected code

`AddRealComp`, `AddIntComp`, `ResizeRuntimeRealComp`, and
`ResizeRuntimeIntComp` in `Src/Particle/AMReX_ParticleContainer.H` and
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

All four routines update existing tiles through `ParIterType`. `ParIterBase`
explicitly skips a tile unless `numParticles() > 0`; that count excludes
neighbor particles. A tile holding only neighbors is therefore left with the
old runtime-component layout while the container's counts and communication
masks immediately switch to the new layout.

Later neighbor packing, copying, or access uses the container-wide component
count against a tile whose pointer arrays have a different length. This can
assert in debug builds and access invalid component pointers in optimized
builds.

## Proposed patch

Iterate the actual `m_particles` maps and redefine every existing tile,
including empty and neighbor-only entries. Preserve both real and neighbor
particle counts while resizing component arrays. Alternatively, reject layout
changes while neighbor storage exists and require a documented clear/refill
sequence.

Add a regression with a neighbor-only tile, then add and resize real/int
runtime components and exercise neighbor pack/unpack.

## Verification

**Conclusion: Confirmed.**

All four runtime-layout mutators traverse tiles through `ParIterType`.
`ParIterBase_impl` advances only to entries whose `numParticles() > 0`, and
that count explicitly excludes neighbors. A neighbor-only tile is skipped
while the container-wide runtime counts and communication masks change.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The inconsistent tile layout requires both AMReX runtime-component mutation
and a tile populated exclusively by neighbor particles. Quokka never calls the
runtime add/resize mutators, and its species use ordinary
`AmrParticleContainer` types rather than `NeighborParticleContainer`, so they
do not create neighbor-only tiles.

These are structural preconditions of the bug, not merely unusual runtime
values. With no neighbor storage and no runtime layout transition, Quokka
cannot change the container-wide component counts while leaving a skipped
tile at the old layout.

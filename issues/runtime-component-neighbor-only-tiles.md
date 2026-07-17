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

# Particle routing masks remain stale after geometry changes

## Severity

High

## Affected code

The geometry setters, `ParticleContainerBase::BuildRedistributeMask`, and
`NeighborParticleContainer_impl::areMasksValid`, and the GPU
`buildNeighborMask` cache in
`Src/Particle/AMReX_ParticleContainerBase.cpp` and
`Src/Particle/AMReX_NeighborParticlesI.H` /
`Src/Particle/AMReX_NeighborParticlesGPUImpl.H`.

## Explanation

The redistribute and neighbor masks store grid/tile IDs in valid cells and
fill ghost cells using the current geometry's periodicity. Whether a particle
just beyond a domain boundary maps to a periodic grid therefore depends on the
`Geometry`.

Both cache checks are driven by grow width and BoxArray/DistributionMapping
references. `SetParticleGeometry` can replace the geometry while leaving those
objects unchanged, and the multi-level `SetParGDB`/`Define` paths can do the
same. A later mask request then reuses ghost values filled under the old
periodicity. The neighbor validity check recomputes radius grow requirements,
but it still does not compare periodicity or other geometry identity.

Changing a direction between periodic and non-periodic can consequently route
particles through an obsolete periodic image or fail to find a destination
that now exists. The cached `neighbor_procs` derived from that mask is stale as
well.

## Proposed patch

Invalidate `redistribute_mask_ptr`, `redistribute_mask_nghost`, its cached
neighbor ranks, and every neighbor `mask_ptr` whenever particle geometry is
replaced. Alternatively, include the relevant geometry/periodicity identity in
all cache validity tests. The GPU cache should also clear stale code/intersection
arrays and process ranks when it is rebuilt.

Add a test that builds a mask with a non-periodic geometry, replaces it with an
otherwise identical periodic geometry, rebuilds at the same grow width, and
checks the domain-edge ghost mapping and neighbor ranks (and the reverse
transition).

## Verification

**Conclusion: Confirmed.**

`BuildRedistributeMask()` and both neighbor-mask validity checks key their
caches on grow width and grid/distribution references, not geometry or
periodicity. `SetParticleGeometry()` replaces only the database geometry and
does not clear these caches. A same-grid periodicity change therefore reuses
ghost-cell routing and neighbor-rank state filled under the old geometry.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The stale caches are consumed by the optimized local-redistribution and
neighbor-particle paths. Quokka's `Redistribute()` call sites leave the
`local` redistribution distance at zero, so AMReX builds the normal global
copy plan instead of consulting `BuildRedistributeMask()`. Quokka also has no
`NeighborParticleContainer`, excluding the two neighbor-mask consumers.

Quokka may replace or redefine particle geometry during restart/refinement,
but no later Quokka operation reads one of the caches that this issue leaves
stale. Because both possible consumers are disabled by the current container
type and redistribution arguments, a geometry or periodicity change cannot
activate the reported failure.

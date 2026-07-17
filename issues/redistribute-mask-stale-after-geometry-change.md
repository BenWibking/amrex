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

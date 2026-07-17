# Actual-neighbor selection underallocates boundary IDs

## Severity

High

## Affected code

`NeighborParticleContainer_impl::selectActualNeighbors` in
`Src/Particle/AMReX_NeighborParticlesI.H`.

## Explanation

The output vector stores the index of each real particle that interacts with
at least one particle outside its tile. Its maximum possible length is
`np_real`, but the implementation allocates only
`pti.numNeighborParticles()` entries before atomically appending real indices.

There is no one-to-one relation between these counts. Many real particles near
a boundary can all interact with the same single neighbor particle. In that
case the atomic counter exceeds the one-element allocation and GPU threads
write past `p_boundary_particle_ids`, corrupting device memory. The later
resize to the final counter cannot repair the overwrite.

## Proposed patch

Allocate the temporary boundary-ID vector to `np_real`, the true upper bound,
then shrink it to the copied counter after the kernel. A two-pass flag/scan can
avoid the upper-bound allocation if memory pressure warrants it.

Add a GPU regression with many local boundary particles and one external
neighbor, checking both the selected ID count and sanitizer-clean execution.

## Verification

**Conclusion: Confirmed.**

`selectActualNeighbors()` launches one thread per real particle and can append
one boundary ID from each thread, but sizes the destination to the number of
neighbor particles. Those counts have no bounding relationship: many real
particles can match the same external neighbor, so the atomic append can
exceed the allocation.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This defect exists only in `NeighborParticleContainer_impl`'s actual-neighbor
selection workflow. Quokka instantiates ordinary `AmrParticleContainer`s and
`AmrTracerParticleContainer`; its source contains no
`NeighborParticleContainer`, `selectActualNeighbors`, `fillNeighbors`, or
`updateNeighbors` call. Quokka's particle ghost handling is AMR
redistribution/deposition, not the neighbor-particle halo subsystem that owns
the undersized boundary-ID vector.

Because the affected template is never instantiated or called by Quokka, no
particle count or grid arrangement in a current Quokka run can reach this
allocation. Using this issue would require adding a new neighbor-particle
container feature to Quokka, so the exclusion is structural and supports the
definite category.

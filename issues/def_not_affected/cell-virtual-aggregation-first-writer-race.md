# Cell virtual aggregation has a racy first-particle election

## Severity

High

## Affected code

The integer and unweighted-real deposition kernels in the `Cell` branch of
`ParticleContainer_impl::CreateVirtualParticles` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

The integer kernel tests `partData(iv,0) == 0` and then atomically increments
that value. The test and increment are not one atomic operation. Multiple
particles in the same cell can all observe zero and each increment the
occupancy marker.

The later exclusive scan treats that marker as a count, but emits only one
virtual particle for every nonzero cell. Values greater than one therefore
create holes and leave entries in the output tile unwritten. The similar
check-before-add logic used for unweighted array-real fields can sum values
from multiple alleged winners rather than selecting one particle's value.

## Proposed patch

Use an atomic compare-and-swap on a dedicated owner/occupancy flag. Only the
successful thread should copy unweighted real and integer fields, and every
occupied cell must contribute exactly one to the scan. Keep weighted sums in
their separate atomic accumulators.

Add a highly contended one-cell CPU/GPU regression and verify the output count,
IDs, and all copied fields over repeated runs.

## Verification

**Conclusion: Confirmed.**

The integer deposition callback tests `partData(iv,0) == 0` separately from
its atomic increment. Multiple threads can pass the test and each increment
the occupancy count, but reconstruction emits only one particle for any
nonzero cell. The scan can therefore reserve holes, and the analogous
check-before-add for unweighted fields can combine multiple contenders.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The racy kernel exists only in the `Cell` aggregation branch of
`ParticleContainer::CreateVirtualParticles`. Quokka never calls
`CreateVirtualParticles` and never sets `particles.aggregation_type` to
`Cell`; its AMR particle coupling deposits real particles directly and uses
redistribution rather than AMReX virtual-particle aggregation.

No Quokka timestep, regrid, checkpoint, or diagnostic can therefore launch
the first-writer election kernel. Adding a virtual-particle gravity algorithm
would be a new subsystem integration, so the current exclusion is definite.

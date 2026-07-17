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

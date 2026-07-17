# Cell virtual aggregation divides by a zero aggregate weight

## Severity

Medium

## Affected code

The reconstruction kernel in the `Cell` branch of
`ParticleContainer_impl::CreateVirtualParticles` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

The branch treats real component zero as a weight, sums it per cell, and then
unconditionally divides weighted positions and other struct-real fields by
that sum. Particles are not required here to have nonzero or same-sign weights.

A cell containing only zero-weight particles, or weights that cancel, produces
division by zero and stores NaN or infinity in the virtual particle. The
particle remains marked with a valid virtual ID and can contaminate later mesh
or particle operations.

## Proposed patch

Define the accepted weight contract. If weights must be strictly positive,
validate it while depositing and fail with a clear diagnostic. Otherwise,
handle a zero aggregate explicitly by skipping the virtual particle or using a
documented unweighted fallback, with the occupancy scan adjusted accordingly.

Add zero, canceling-sign, and ordinary positive-weight aggregation tests.

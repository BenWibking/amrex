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

## Verification

**Conclusion: Confirmed.**

Cell aggregation sums real component zero and unconditionally divides weighted
positions and struct-real fields by that sum. There is no positivity or
nonzero check during deposition or reconstruction, so zero or canceling
weights create valid-ID virtual particles containing infinities or NaNs.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Zero aggregate weight is consumed only by the `Cell` reconstruction kernel in
`CreateVirtualParticles`. Quokka never calls that method or selects cell
virtual-particle aggregation. Quokka's mass and radiation deposition kernels
operate on real particles with their own physical checks and do not reconstruct
a weighted virtual particle by dividing through AMReX component zero.

Therefore even a zero-mass or canceling-weight particle set in Quokka cannot
reach this particular division. The affected algorithm is absent, yielding a
definite non-impact classification.

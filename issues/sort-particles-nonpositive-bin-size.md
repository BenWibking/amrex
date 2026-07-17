# Particle bin sorting accepts nonpositive component sizes

## Severity

High

## Affected code

`ParticleContainer_impl::SortParticlesByBin` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

The routine returns only when the entire `IntVect` equals the zero vector. A
mixed value such as `(1,0,1)`, or any negative component, proceeds to
`numTilesInBox` and `GetParticleBin`, where components are used as divisors and
bin extents.

Zero components can cause integer division by zero, while negative sizes
produce invalid bin counts and particle permutations. The all-zero special
case makes the partial-zero failure especially surprising.

## Proposed patch

Require every active-dimensional component to be strictly positive. If an
all-zero vector is meant to mean "sorting disabled," retain that sentinel but
reject every other nonpositive vector with an always-on diagnostic.

Add tests for all-zero, mixed-zero, negative, and valid anisotropic bin sizes.

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

## Verification

**Conclusion: Confirmed.**

`SortParticlesByBin()` special-cases only equality with the all-zero vector.
Mixed-zero and negative vectors proceed into `numTilesInBox()` and the bin
mapper, where the components are used in division and extent calculations.
The documented no-op sentinel does not cover these invalid inputs.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The invalid division path is entered only through
`SortParticlesByBin()` with a mixed-zero or negative bin-size vector. Quokka
does not call `SortParticlesByBin()` or the related particle-by-cell sorting
API; its deposition and update loops operate directly on the existing
particle tiles.

No Quokka input is consequently converted into this AMReX bin-size argument.
Since the affected sorter is absent from the Quokka call graph, neither an
unvalidated user value nor an internally generated vector can reach its
nonpositive extent calculations.

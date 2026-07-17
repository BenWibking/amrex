# ASCII replication accepts zero and negative factors

## Severity

High

## Affected code

`ParticleContainer_impl::InitFromAsciiFile` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

When `Nrep` is supplied, its components are copied without validation. Domain
sub-lengths are then computed by dividing by each component. A zero factor
causes division by zero; a negative factor produces a negative sub-length and
replication loops whose behavior no longer matches the requested domain
tiling.

## Proposed patch

Require all active-dimensional replication factors to be strictly positive
before computing `DomSize`. Report the offending direction and value.

Add tests for zero, negative, unity, and anisotropic positive replication
vectors.

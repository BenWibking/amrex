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

## Verification

**Conclusion: Confirmed.**

`InitFromAsciiFile()` copies `Nrep` and divides each active domain length by
its component before any positivity check. Zero divides by zero, while a
negative component creates a negative sub-length and prevents the associated
positive replication loop from representing the requested tiling.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka uses `InitFromAsciiFile` in many particle problems, but every checked-in
call omits the replication argument and therefore receives AMReX's default
unit replication vector. The filenames and number of extra real fields vary;
the replication factor does not. A unit vector is strictly positive and
cannot take either the divide-by-zero or negative-loop path described here.

This is not a definite exclusion because the affected API is directly exposed
to Quokka problem specializations. A downstream Quokka problem can pass an
explicit `Nrep`, and Quokka performs no wrapper validation before forwarding
it to AMReX. The current repository does not contain such a call or a runtime
parameter that produces a nonpositive factor, so the issue is unlikely to
affect existing Quokka problems while remaining reachable to future or
out-of-tree problem code.

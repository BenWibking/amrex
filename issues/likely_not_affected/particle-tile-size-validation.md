# Particle tiling accepts zero or negative tile extents

## Severity

High

## Affected code

`ParticleContainer_impl::Initialize` in
`Src/Particle/AMReX_ParticleContainerI.H` and downstream tile-index helpers.

## Explanation

When `particles.tile_size` is supplied, `Initialize` copies every value into
the static `tile_size` without validating it. A zero or negative extent is
then used by particle tiling and by `getTileIndex`/`numTilesInBox`, which use
the extent in integer division and tile-count calculations.

With tiling enabled, a zero component can therefore cause division by zero;
negative components can create invalid tile counts and indices. The error is
reported far from the input that caused it and may be build-mode dependent.

## Proposed patch

After parsing, require every active-dimensional component of `tile_size` to be
strictly positive whenever particle tiling is enabled. Reject invalid input
with an always-on message that names `particles.tile_size` and the offending
component.

Add parameter-parsing tests for zero and negative extents plus a positive
anisotropic tile size.

## Verification

**Conclusion: Confirmed.**

`Initialize()` copies every parsed `particles.tile_size` component into the
static `tile_size` with no validation. When tiling is enabled, downstream tile
count/index helpers divide by these values, so zero causes division by zero and
negative extents produce invalid counts.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka does not set `particles.tile_size` in its checked-in inputs or source,
so its particle containers use AMReX's positive backend-specific defaults.
Those defaults produce valid tile counts for the grids Quokka constructs, and
normal particle initialization and redistribution therefore never divide by
a zero or negative tile extent.

Quokka does not wrap or reject the generic AMReX parameter, however. A user can
add `particles.tile_size` to any Quokka input, including invalid components,
and the failure would occur while constructing ordinary Quokka particle
containers. Since the exact bad values are absent from current decks but still
part of the accepted parameter surface, likely not affected is more accurate
than definitely not affected.

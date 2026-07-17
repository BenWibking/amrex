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

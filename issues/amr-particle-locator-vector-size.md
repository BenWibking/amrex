# AMR particle locator build does not validate geometry vector length

## Severity

Medium

## Affected code

The vector overload of `AmrParticleLocator::build` in
`Src/Particle/AMReX_ParticleLocator.H`.

## Explanation

The function derives `num_levels` solely from `a_ba.size()` and then indexes
`a_geom[lev]` for every level. It never checks that the geometry vector has the
same length. A shorter geometry vector causes an out-of-bounds read during
construction; a longer one is silently truncated, usually indicating a
misconfigured hierarchy.

Because the constructor taking these vectors forwards directly to `build`, the
unsafe behavior occurs during ordinary public construction and cannot be
detected by the caller afterward.

## Proposed patch

Require equal vector lengths with an `AMREX_ALWAYS_ASSERT_WITH_MESSAGE` before
marking the locator defined or resizing internal state. Include both sizes in
the diagnostic. Apply the same invariant when refreshing geometry from a
`ParGDBBase` whose level count differs from the built locator.

Add construction tests for equal, shorter, and longer geometry vectors,
including zero levels.


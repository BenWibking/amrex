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

## Verification

**Conclusion: Confirmed.**

The vector `AmrParticleLocator::build()` derives its loop bound solely from
`a_ba.size()` and directly indexes `a_geom[lev]`. There is no equality check,
so a shorter geometry vector is read out of bounds and a longer hierarchy is
silently ignored through this public constructor/build path.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka does not call the affected vector overload with independently assembled
geometry and `BoxArray` vectors. Its main particle containers use the
`AmrCore`/`AmrParGDB` interface, from which AMReX obtains geometry and grid
metadata for the same active level range. Even the restart-refinement helper
restores geometry, box arrays, and distribution maps for every level before it
redefines and redistributes the container, so the corresponding vectors are
constructed from one `finest_level` value.

The classification remains probabilistic because Quokka dynamically replaces
particle hierarchy metadata during restart refinement and regridding, and the
same locator implementation is rebuilt afterward. There is no explicit
length check protecting a future mismatch. The current code assembles the
inputs coherently, so the issue is unlikely to affect Quokka today, but the
shared dynamic path prevents a stronger definite-not-affected conclusion.

# Runtime-only particle tile accepts negative and non-position indices

## Severity

Medium

## Affected code

`ArrayView`, `ParticleTileDataRT`, and `RTSoAParticle::pos` in
`Src/Particle/AMReX_ParticleTileRT.H`.

## Explanation

The runtime-only tile uses signed `Long` particle indices. Its element
accessors assert only `index < capacity`, so every negative value passes and is
used to index before the allocation. This affects `ArrayView::operator[]`,
particle identity access, component access, `operator[]`, and packing/unpacking
source or destination indices.

Position access also checks a direction against `m_n_real` rather than
`AMREX_SPACEDIM`. Because runtime-only tiles normally contain additional real
components, `particle.pos(AMREX_SPACEDIM)` can silently return the first
non-position component instead of rejecting the invalid direction.

## Proposed patch

Require `0 <= index && index < capacity` everywhere a signed particle index is
accepted, validate nonnegative buffer offsets, and bound position directions by
`AMREX_SPACEDIM` (while separately asserting the tile has at least that many
real components).

Add boundary/death tests for `-1`, zero, the final valid particle, capacity,
`-1` directions, and `AMREX_SPACEDIM` directions on both mutable and const
views.

## Verification

**Conclusion: Confirmed.**

The runtime tile's `ArrayView` and `ParticleTileData` accessors check signed
indices only against an upper bound, so negative indices pass. Position access
also checks a direction against the number of real components rather than
`AMREX_SPACEDIM`, exposing non-position storage when extra real components
exist.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This report concerns the runtime-only `ParticleTileRT`, `ParticleTileDataRT`,
and `RTSoAParticle` types. Quokka defines no runtime-only particle tile and
never references these APIs; all positions and fields live in compile-time AoS
particle types. Quokka's dynamic particle-type registry selects among those
compile-time C++ types and does not change their storage layout.

No component or position index in Quokka can reach the RT-specific accessors.
Using them would require a new container architecture, making current
non-impact definite.

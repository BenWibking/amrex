# EnsureThreadSafeTiles skips the tiles it must create

## Severity

High

## Affected code

`EnsureThreadSafeTiles` in `Src/Particle/AMReX_ParticleUtil.H` and the
cross-container neighbor-list builders that rely on it.

## Explanation

The helper says it ensures tile entries exist before parallel code accesses
the particle maps. It iterates with `PC::ParIterType`, however, and `ParIter`
intentionally advances past map entries that are absent or contain zero real
particles. Calling `DefineAndReturnParticleTile` from that loop therefore only
revisits tiles already represented by nonempty particle entries.

This is observable in cross-container neighbor-list construction. The builder
calls `EnsureThreadSafeTiles(other)` and then, for each nonempty source tile,
uses `other.ParticlesAt(lev, pti)`. If the target container has no map entry at
a source tile, `ParticlesAt` calls `map::at` and throws. If entries were instead
created lazily inside a parallel region, concurrent map mutation would be
unsafe—the exact condition the helper is meant to prevent.

## Proposed patch

Iterate over every local grid/tile using `pc.MakeMFIter(lev)` (or an equivalent
plain `MFIter`) and call `DefineAndReturnParticleTile` for each one. Do not use
the particle iterator, whose contract excludes empty tiles.

Add a cross-container neighbor-list test in which the source has particles on
a tile for which the target container has no pre-existing entry. Run both the
serial and OpenMP paths and verify an empty target tile is created safely.

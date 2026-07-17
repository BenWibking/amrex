# ParticleTile swap does not validate runtime-component layouts

## Severity

Medium

## Affected code

`ParticleTile::swap` in `Src/Particle/AMReX_ParticleTile.H`.

## Explanation

`ParticleTile::swap` is a storage-only swap: it deliberately preserves each
tile's definition, names, arena, and neighbor count. That requires the two
tiles to have identical real and integer component layouts.

The implementation does not enforce that precondition. It loops over
`this->NumRealComps()` and `this->NumIntComps()` and indexes the other tile at
each component. If the left tile has more runtime components, the other tile's
`GetRealData(j)` or `GetIntData(j)` is out of range. In assertion-enabled
builds this trips an assertion; optimized builds can access beyond the runtime
component vector. If the left tile has fewer components, the unmatched arrays
on the right are not swapped, leaving the operation only partially complete.

The fixed template component counts do not prevent this: runtime components
are established separately by `define`, and a default-constructed temporary
has none.

## Proposed patch

Add always-on checks at the beginning of `swap` requiring equal real and
integer component counts and compatible allocator arenas. Document that the
operation exchanges particle storage while retaining tile configuration.

Alternatively, make the operation a full object swap, but then update callers
such as `ReorderParticles` to initialize and preserve neighbor counts and other
container-owned metadata explicitly. Add tests swapping tiles with matching
runtime layouts and rejecting mismatched real and integer layouts.

## Verification

**Conclusion: Confirmed.**

`ParticleTile::swap()` is public for tiles of the same static type, while their
runtime component counts can differ. It loops over the receiving tile's counts
and indexes the other tile's arrays, causing partial swaps or out-of-bounds
access for two valid, differently configured tiles. No runtime-layout
precondition is documented or enforced.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The failure requires swapping two tiles with different runtime real or integer
component counts. Every Quokka particle container has zero runtime components.
AMReX's internal temporary tiles on Quokka paths are therefore uniformly
defined with the same zero-runtime layout, so a swap cannot encounter unequal
runtime arrays. Quokka also has no direct cross-container `ParticleTile::swap`
call.

Compile-time AoS layouts cannot be changed by this runtime swap method. The
necessary unequal runtime configuration is impossible for current Quokka,
making the exclusion definite.

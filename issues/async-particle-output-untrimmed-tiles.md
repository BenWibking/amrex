# Async particle output writes uninitialized tile tails

## Severity

High

## Affected code

The pinned-tile staging loop in `WriteBinaryParticleDataAsync` in
`Src/Particle/AMReX_WriteBinaryParticleData.H`.

## Explanation

`np_per_grid_local[lev][grid]` is the number of valid particles summed over
all tiles of a grid. The async staging loop uses that grid-wide value to resize
every individual `new_ptile`, then calls `filterParticles` for one source tile:

```cpp
new_ptile.resize(np_per_grid_local[lev][mfi.index()]);
amrex::filterParticles(new_ptile, ptile, KeepValidFilter());
```

`filterParticles` returns the number of particles it copied but does not resize
the destination. The return value is ignored. Consequently each staged tile
retains a tail sized according to the entire grid, not its own selected count.
Those elements were never populated.

The writer later iterates through `pbox.numParticles()` and treats an element
as output whenever its ID appears valid. Because the unused ID/CPU storage is
not guaranteed to be initialized to an invalid ID, garbage tail elements can
be serialized as particles. With multiple tiles, the oversized tail is also
repeated once per tile.

## Proposed patch

Size the temporary to the source tile's particle count, capture the return
value from `filterParticles`, and resize the temporary to that selected count
before storing or writing it. Avoid using the grid aggregate as a tile size.

Add an async-output regression with multiple tiles on one grid, a mixture of
valid and invalid IDs in each tile, and a read-back check for exact particle
count and values.

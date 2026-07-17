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

## Verification

**Conclusion: Confirmed.**

Each pinned tile is resized to its grid's aggregate valid count, then filtered
from only one source tile. The returned selected count is ignored and the
destination is never shrunk. Later loops traverse `pbox.numParticles()`, so
uninitialized per-tile tails are treated as candidate records and the grid-
wide over-allocation is repeated for every tile.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka uses tiled particle containers and can have multiple particle tiles per
grid, so the underlying layout is relevant. However, the faulty pinned-tile
staging loop is exclusive to AMReX's async particle writer. Quokka explicitly
rejects `amrex.async_out`, none of its checked-in inputs enables the option,
and supported checkpoint and plotfile output therefore uses the synchronous
packing path, which does not create these untrimmed pinned tiles.

A definite exclusion would overstate the guard: Quokka still forwards the
AMReX option and can issue an initial checkpoint before reaching its async
abort. An unsupported run with async output and particles could enter the
staging loop before termination. Because the relevant tile shapes are normal
for Quokka but the writer is disabled in supported use, likely not affected is
the appropriate confidence level.

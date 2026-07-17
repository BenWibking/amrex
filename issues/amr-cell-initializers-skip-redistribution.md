# AMR per-box and per-cell initializers leave particles on the wrong rank

## Severity

High

## Affected code

`InitRandomPerBox` and `InitNRandomPerCell` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

Both routines iterate locally owned level-zero grids, call `Where`, and insert
using the returned finest-level grid/tile key. In an AMR hierarchy, the owning
rank of an overlapping fine grid need not be the rank that owns the coarse
source grid.

Unlike `InitOnePerCell` and the parallel `InitRandom` path, these functions do
not call `Redistribute` after insertion. A rank can retain map entries keyed to
remote fine grids; particle iterators on both ranks then miss or mis-own those
particles and the container may fail `OK()`.

## Proposed patch

Redistribute after generation, or route each staged particle directly to the
owner returned by the fine-level distribution map. Add an `OK()` check after
the operation in debug/testing builds.

Add a two-level regression whose fine and coarse distribution mappings
deliberately assign overlapping boxes to different ranks.

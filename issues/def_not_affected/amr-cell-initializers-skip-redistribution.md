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

## Verification

**Conclusion: Confirmed.**

Both initializers generate work from locally owned level-zero grids, call
`Where()` (which can select a finer grid owned by another rank), and insert
under that returned key in the local map. Neither routine redistributes at the
end, unlike `InitOnePerCell()` and parallel `InitRandom()`.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The affected entry points are specifically `InitRandomPerBox` and
`InitNRandomPerCell`. Quokka does not call either one. Its checked-in creation
paths use `InitFromAsciiFile` for file-backed physics particles,
`InitRandom` for spherical collapse, `InitOnePerCell` for tracers, or
Quokka-owned GPU kernels that insert into `DefineAndReturnParticleTile` and
then redistribute. Those are separate AMReX implementations with their own
post-initialization behavior.

Consequently Quokka cannot enter the two routines that retain remotely owned
fine-grid keys in a local map. The general fact that Quokka uses AMR particles
does not connect it to this method-specific defect; adding one of these
initializers would be a new call site, so current impact is definitely absent.

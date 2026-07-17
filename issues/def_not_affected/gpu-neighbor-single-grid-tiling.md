# GPU neighbor fill skips tiles on a single grid

## Severity

High

## Affected code

The single-grid early returns in `buildNeighborMask` and
`buildNeighborCopyOp` in `Src/Particle/AMReX_NeighborParticlesGPUImpl.H`, and
in `selectActualNeighbors` in `Src/Particle/AMReX_NeighborParticlesI.H`.

## Explanation

The GPU path returns immediately whenever level 0 has one BoxArray box and is
not periodic. That condition proves there is no inter-grid or periodic
communication, but it does not prove there is only one particle tile.

When particle tiling is enabled, a single grid can contain many tiles.
Particles within the neighbor width of a tile face must be copied to adjacent
tiles on the same grid. The early returns leave the neighbor copy operation
empty, so `fillNeighborsGPU` creates no such particles. Neighbor lists then
miss interactions across internal tile boundaries. The actual-neighbor
selection shortcut repeats the same invalid assumption.

## Proposed patch

Only take the shortcut when tiling is disabled or the grid produces exactly
one tile. A simpler safe fix is to remove the early returns and let the normal
self-exclusion logic discard only the exact source grid/tile with zero periodic
shift.

Add a GPU test with one non-periodic grid split into at least two particle
tiles and interacting particles on opposite sides of an internal tile face.
Verify fill, neighbor-list construction, and boundary-only update.

## Verification

**Conclusion: Confirmed.**

The GPU mask and copy-op builders return whenever a non-periodic level has one
grid, without checking `do_tiling`. `forEachIntersectingTile()` otherwise
contains the needed same-grid tile enumeration and excludes only the exact
source tile, confirming that internal tile faces are part of the intended copy
path.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This early return is evaluated only by GPU neighbor-mask construction and
actual-neighbor selection in `NeighborParticleContainer`. Quokka does use
particle tiling, including single-grid problems, but it never instantiates a
neighbor container or requests neighbor copies among those tiles. Standard
`Redistribute` handles Quokka particle ownership without these routines.

As a result the combination of one grid and multiple tiles cannot reach the
faulty return in a Quokka executable. The matching container subsystem is
absent, making the classification definite.

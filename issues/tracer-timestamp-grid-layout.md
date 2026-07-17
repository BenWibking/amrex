# Tracer timestamping indexes a MultiFab without verifying matching grids

## Severity

High

## Affected code

`TracerParticleContainer::Timestamp` in
`Src/Particle/AMReX_TracerParticles.cpp`.

## Explanation

The function iterates the particle container's `(grid, tile)` map but uses each
particle grid id directly to access `ba[grid]` and `mf[grid]`. Unlike
`AdvectWithUcc`, it never verifies `OnSameGrids(lev, mf)`.

If the supplied `MultiFab` has a different `BoxArray`, the same integer grid id
refers to a different spatial box or may be out of range. If it has a different
`DistributionMapping`, a particle-local grid need not be local in the
`MultiFab`. The subsequent interpolation can therefore read the wrong field
data, access a nonlocal/missing FAB, or read outside the selected FAB. No
same-layout precondition is documented in the declaration.

## Proposed patch

Choose and enforce the API contract explicitly:

1. For the narrow fix, require `OnSameGrids(lev, mf)` with an always-on,
   descriptive check before any file is opened, and document the requirement.
2. If different layouts are intended to be supported, create a temporary
   `MultiFab` on the particle level's `BoxArray` and `DistributionMapping`,
   `ParallelCopy` the selected source data into it (including the interpolation
   stencil's ghost cells and periodicity), and index that aligned copy.

Add tests for mismatched `BoxArray` and mismatched `DistributionMapping` so the
chosen behavior is explicit and safe.


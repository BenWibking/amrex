# Binary initialization consolidates only AoS particle data

## Severity

High

## Affected code

`ParticleContainer_impl::InitFromBinaryFile` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

The reader accepts the generic hybrid container type but reads extra values
only into struct-real fields. Compile-time/runtime SoA arrays are left
uninitialized. After each redistribution batch, it moves particles into
`tmp_particles` by inserting only each tile's AoS vector and then discards the
source `ParticleLevel`.

Any SoA data that did exist is therefore lost during consolidation, and the
final tiles do not have a schema consistent with the container. Pure-SoA types
cannot instantiate the local default `ParticleType` path at all.

## Proposed patch

Either constrain this legacy format/API to AoS-only containers with an
explicit diagnostic, or stage and consolidate complete `ParticleTile` data,
including compile-time/runtime real/int arrays and ID/CPU storage. Avoid moving
only one storage half of a hybrid tile.

Add multi-batch hybrid and pure-SoA tests that verify every component after
initialization.

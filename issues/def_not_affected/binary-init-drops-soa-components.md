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

## Verification

**Conclusion: Confirmed.**

The binary path reads and stages only `ParticleType` AoS records. Although
temporary hybrid tiles are resized, no SoA values are initialized, and after
each batch the consolidation loop inserts only each tile's AoS vector into a
new default tile before discarding the original map. The final hybrid tile can
therefore have AoS particles with zero-length SoA arrays.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The bug is confined to `InitFromBinaryFile`, and its data loss additionally
requires compile-time or runtime SoA components. Quokka has no
`InitFromBinaryFile` call: checked-in particle initial conditions come from
ASCII files, `InitRandom`, `InitOnePerCell`, or Quokka-owned creation kernels.
Its particle aliases also have zero SoA array components.

Thus Quokka neither invokes the faulty consolidation loop nor owns data that
the loop could drop. Binary checkpoint restart uses the separate `Restart`
format/path and is not this raw binary initializer. Both exclusions are
structural, so current Quokka is definitely unaffected.

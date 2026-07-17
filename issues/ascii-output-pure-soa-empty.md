# ASCII output writes pure-SoA containers as empty

## Severity

High

## Affected code

`ParticleContainer_impl::WriteAsciiFile` in
`Src/Particle/AMReX_ParticleIO.H`.

## Explanation

Both the counting pass and the writing pass use
`GetArrayOfStructs().numParticles()` as the particle count. A pure-SoA tile
stores ID/CPU, positions, and attributes entirely in its struct-of-arrays, so
its AoS length is zero regardless of the real particle count.

The routine consequently writes a header claiming zero particles and emits no
records for a nonempty pure-SoA container. There is no warning that this public
output path supports only AoS/hybrid layouts.

## Proposed patch

Iterate `tile.numParticles()` and access ID, CPU, positions, and attributes via
`ConstParticleTileData` or a gathered super-particle, so all layouts share one
record path. Define whether pure-SoA position arrays are repeated in the ASCII
attribute section and keep the metadata consistent with that choice.

Add exact ASCII count/data tests for AoS, hybrid, and pure-SoA containers.

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

## Verification

**Conclusion: Confirmed.**

Both passes in `WriteAsciiFile()` derive `np` from
`GetArrayOfStructs().numParticles()`. A pure-SoA tile stores its particles in
ID/CPU and SoA arrays and has a permanently empty AoS, so a nonempty container
is counted as zero and produces no records.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka has neither prerequisite for this defect. Its particle types are AoS
`Particle<NReal,NInt>` containers rather than pure-SoA containers, and Quokka
does not call AMReX `WriteAsciiFile`. Particle plotfiles/checkpoints use the
binary particle writer, while Quokka's optional human-readable export in
`particle_IO.hpp` explicitly gathers AoS particles and writes its own text
format.

The affected function therefore sees no Quokka container, and the affected
pure-SoA representation is not instantiated. Reaching the bug would require
both a new pure-SoA particle type and a new `WriteAsciiFile` call, which makes
the current non-impact definite.

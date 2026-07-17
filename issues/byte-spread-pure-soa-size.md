# ByteSpread uses the proxy-object size for pure-SoA particles

## Severity

Low

## Affected code

`ParticleContainer_impl::ByteSpread` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

The per-particle byte estimate always starts with `sizeof(ParticleType)`. For a
pure-SoA container, `ParticleType` is `SoAParticle`, a transient proxy that
contains tile-data pointers and an index; it is not stored once per particle.
The actual fixed per-particle storage is the 64-bit packed ID/CPU value.

As a result, the printed and returned byte spread can substantially overstate
pure-SoA memory while appearing to be an exact byte count. `SetParticleSize`
already handles this distinction correctly.

## Proposed patch

Use `sizeof(uint64_t)` for pure-SoA ID/CPU storage and `sizeof(ParticleType)`
for AoS storage, then add all compile-time/runtime SoA arrays. Prefer sharing a
single storage-size helper with `SetParticleSize`.

Add an exact one-particle test for AoS, hybrid, and pure-SoA containers.

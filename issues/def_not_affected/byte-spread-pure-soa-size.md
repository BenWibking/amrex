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

## Verification

**Conclusion: Confirmed.**

`ByteSpread()` always charges `sizeof(ParticleType)` per particle. For pure SoA
that type is an accessor proxy, whereas stored fixed data is the packed ID/CPU
word; compile-time and runtime SoA arrays are added separately. The returned
byte estimate therefore counts non-stored proxy fields and omits the actual
fixed representation.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The incorrect size expression is selected only for pure-SoA particle types.
Quokka's tracers and physics particles are AoS containers, so their
`ByteSpread` instantiations, if any, would use the ordinary AoS particle size.
More directly, Quokka never calls `ByteSpread` or `PrintCapacity`, the two
interfaces that expose this diagnostic estimate.

The issue affects a memory-reporting utility for a layout Quokka does not own;
it is not used in allocation, checkpoint, or redistribution decisions in
Quokka. Reaching it would require a new pure-SoA type plus a new diagnostic
call, so current impact is definitely absent.

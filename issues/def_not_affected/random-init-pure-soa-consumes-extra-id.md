# Parallel pure-SoA random initialization consumes two IDs per particle

## Severity

Medium

## Affected code

The `serialize == false` pure-SoA branch of
`ParticleContainer_impl::InitRandom` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

The parallel loop first assigns `ptest.id() = ParticleType::NextID()`. The AoS
branch reuses that ID, but the pure-SoA branch ignores it and calls
`ParticleType::NextID()` again when constructing packed ID/CPU storage.

Every generated pure-SoA particle therefore consumes two IDs and retains only
the second. ID progression differs between serialized and parallel modes and
reaches the ID limit twice as quickly in this path.

## Proposed patch

Store the ID generated for `ptest` into the pure-SoA packed ID/CPU value, or
delay ID generation until the layout-specific branch and call `NextID()` once.

Add a next-ID progression test comparing AoS and pure-SoA initialization in
both serialization modes.

## Verification

**Conclusion: Confirmed.**

In parallel `InitRandom()`, the common path assigns `ptest.id()` from
`NextID()`. The pure-SoA branch then ignores that ID and invokes `NextID()`
again when filling packed ID/CPU storage. Only the second value is retained,
so the counter advances twice per generated particle.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka does use `InitRandom()` in the spherical-collapse particle setup, but
its production particle types are `AmrParticleContainer<NReal, NInt>` aliases:
their user-defined real and integer fields are compile-time AoS components,
with no pure-SoA array components. The implementation therefore takes the AoS
initializer branch that keeps the ID already assigned to the temporary
particle.

The extra `NextID()` call is compiled only in the pure-SoA branch that packs ID
and CPU separately from a particle struct. Because no current Quokka particle
type has that storage layout, even Quokka's actual random-initialization call
cannot execute the faulty counter update.

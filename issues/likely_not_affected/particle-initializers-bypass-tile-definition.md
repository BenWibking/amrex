# Particle initializers bypass runtime tile definition

## Severity

High

## Affected code

Direct `GetParticles(...)[key]` and `m_particles[...][key]` insertions throughout
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

These expressions default-construct a `ParticleTile` and immediately resize,
push, or copy into it. They bypass `DefineAndReturnParticleTile`, which installs
the container's runtime-component counts, name vectors, and arena.

For a container with runtime components, the new tile has shorter pointer/data
arrays than the container-wide communication layout. With a polymorphic
allocator it can also lack the required arena. Later access or redistribution
can assert, read invalid pointers, or drop components.

## Proposed patch

Route every newly created destination tile through
`DefineAndReturnParticleTile` before mutation. Temporary and accumulation tiles
must likewise be defined with matching runtime counts and the appropriate
arena. Centralize this in a small insertion helper to prevent new init paths
from bypassing the contract.

Add initialization tests using runtime components and
`PolymorphicArenaAllocator` for every affected routine.

## Verification

**Conclusion: Confirmed.**

The initializer paths repeatedly use `GetParticles(...)[key]` or
`m_particles[...][key]`, which default-construct a tile, then immediately
resize or append. Unlike `DefineAndReturnParticleTile()`, this does not install
runtime component counts, names, or a polymorphic arena before mutation.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka directly uses several affected initializers, especially
`InitFromAsciiFile`, `InitRandom`, and `InitOnePerCell`. Its particle aliases,
however, put all current real and integer fields in the compile-time AoS
`Particle<NReal,NInt>` and use AMReX's default allocator. They do not add
runtime SoA components or select a polymorphic custom arena. A
default-constructed tile therefore already has all storage required by the
current Quokka layouts, so bypassing `define()` does not lose Quokka fields or
allocator state.

This is not classified as definitely irrelevant because the exact affected
initializer paths are widespread in Quokka and its particle model continues
to evolve. Adopting runtime components or a custom arena would turn the latent
contract violation into an immediate Quokka failure. Present compile-time AoS
layouts avoid that trigger, making impact unlikely today.

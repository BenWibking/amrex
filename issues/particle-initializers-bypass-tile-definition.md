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

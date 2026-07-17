# AoS transfer overloads are unsafe for containers with SoA components

## Severity

High

## Affected code

The `AoS&` overloads of `AddParticlesAtLevel`, `CreateVirtualParticles`, and
`CreateGhostParticles` in `Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

`AddParticlesAtLevel(AoS&)` swaps only the array-of-structs storage into a
default tile. For a hybrid container, the tile reports particles through its
AoS length but its compile-time SoA arrays have length zero. Copying into the
container then reads those arrays at particle indices and goes out of bounds.

The virtual and ghost AoS overloads have the inverse problem: their temporary
tile may contain valid compile-time and runtime SoA fields, but only the AoS is
swapped into the result, silently discarding every array component.

An `AoS` cannot represent these fields, so the overload cannot fulfill the
container's particle schema.

## Proposed patch

Constrain the AoS overloads to containers with no compile-time or runtime SoA
components, with an always-on runtime check for the latter. Direct all
component-bearing uses to the `ParticleTileType` overload. Document the
restriction and add compile/runtime regressions for hybrid containers.

## Verification

**Conclusion: Confirmed.**

`AddParticlesAtLevel(AoS&)` swaps only AoS storage into a default tile, leaving
all SoA component arrays empty while the tile reports the AoS particle count.
The virtual and ghost AoS overloads do the reverse: they build a full tile and
swap only its AoS out. Any compile-time or runtime SoA fields are respectively
read out of bounds or silently discarded.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The unsafe contract arises only when an AoS-only transfer overload is used on
a container that also owns SoA components. Quokka's containers have no
compile-time or runtime SoA fields, and Quokka does not call the affected AoS
overloads of `AddParticlesAtLevel`, `CreateVirtualParticles`, or
`CreateGhostParticles`. Its restart splitting writes complete AoS particles
directly into defined destination tiles.

With `NArrayReal == NArrayInt == NumRuntime* == 0`, there are no parallel SoA
arrays to lose even if an AoS is copied. The required layout and APIs are both
absent, so Quokka is definitely unaffected.

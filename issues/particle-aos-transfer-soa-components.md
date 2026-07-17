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

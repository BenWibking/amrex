# Several generic initializers cannot instantiate for pure-SoA containers

## Severity

Medium

## Affected code

`InitFromAsciiFile`, `InitFromBinaryFile`, `InitRandomPerBox`,
`InitOnePerCell`, and `InitNRandomPerCell` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

These methods are exposed on `ParticleContainerPureSoA`, but their bodies
default-construct `ParticleType`, write AoS fields, or use AoS-only temporary
tiles. `SoAParticle` is a proxy requiring tile data and an index; it is not a
standalone storage object. Instantiating these methods for a pure-SoA container
therefore fails to compile or cannot preserve the layout.

There are no constraints or API documentation indicating that only
AoS/hybrid containers may call them.

## Proposed patch

Implement layout-independent staging through super-particles/tile data, as
`InitRandom` partially does, or add explicit `requires` constraints and
document the unsupported operations. Prefer shared helpers so pure-SoA and
hybrid initialization receive identical ID, component, and redistribution
semantics.

Add compile-and-run coverage for every initializer on a minimal pure-SoA type,
or compile-time negative tests for intentional constraints.

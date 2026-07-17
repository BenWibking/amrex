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

## Verification

**Conclusion: Confirmed.**

The listed generic method bodies default-construct `ParticleType` and mutate
AoS fields. `SoAParticle` has only a tile-data-plus-index constructor and is an
accessor proxy, not standalone storage. The methods remain public on pure-SoA
containers without constraints, so instantiating them is ill-formed or cannot
represent the full layout.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This is a pure-SoA template-instantiation problem. All Quokka particle aliases,
including tracers, store positions, ID/CPU, and physics fields in AoS
`Particle<NReal,NInt>` objects. None uses `SoAParticle` as its particle type.
The initializers Quokka calls are consequently instantiated on the supported
AoS branches and compile normally.

No runtime parameter can convert those compile-time types into pure SoA. The
affected overloads would matter only after defining new Quokka particle types,
so the existing application is definitely unaffected.

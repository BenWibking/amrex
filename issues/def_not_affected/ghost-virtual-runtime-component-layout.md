# Ghost and virtual output tiles omit runtime components

## Severity

High

## Affected code

The `ParticleTileType&` overloads of `CreateVirtualParticles` and
`CreateGhostParticles` in `Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

Callers commonly pass a default-constructed empty output tile. Neither routine
defines that tile with the container's runtime real/int component counts before
resizing and copying particles. Source tiles can therefore have runtime arrays
while the destination has none.

`copyParticle` requires equal runtime layouts. Debug builds trip its assertion;
optimized builds either omit the values or later read nonexistent source data
when the output is added to a runtime-component container. The `Cell`
aggregation branch also never creates or populates runtime arrays.

## Proposed patch

Define the empty output tile at entry with `m_num_runtime_real`,
`m_num_runtime_int`, the component-name vectors, and the container arena. For
`Cell` aggregation, specify and implement an aggregation policy for runtime
fields or reject runtime-bearing containers before modifying the output.

Add virtual and ghost round-trip tests with both runtime real and runtime
integer components in debug and optimized builds.

## Verification

**Conclusion: Confirmed.**

Both routines accept an empty default tile and resize it without defining the
container's runtime component arrays. Their transforms call `copyParticle()`,
which asserts equal runtime counts and copies according to the destination
layout. Runtime-bearing sources therefore assert in debug builds and lose
fields in optimized builds; the Cell branch also has no runtime-field policy.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This defect requires `CreateGhostParticles` or `CreateVirtualParticles` to
write into a tile from a container with runtime components. Quokka invokes
neither output-tile API, and all current Quokka particle components are
compile-time AoS fields with zero runtime real and integer components.
Quokka's AMR ghost-zone needs are handled on mesh data and through ordinary
particle redistribution, not these AMReX particle copies.

Both the affected operations and the affected layout are absent. The runtime
count assertion or optimized field loss therefore cannot occur in Quokka,
supporting a definite classification.

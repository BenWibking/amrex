# ASCII initialization never stages compile-time SoA real data

## Severity

High

## Affected code

`ParticleContainer_impl::InitFromAsciiFile` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

When `extradata > NStructReal`, `nreals` is resized to exactly
`extradata - NStructReal`. Every condition that pushes or pops those values
tests `nreals.size() > extradata - NStructReal`, which is therefore always
false.

The first redistribution path also guards its array copy with
`std::ssize(std::array<..., NArrayReal>) > NArrayReal`, another condition that
can never be true. The remainder path resizes destination arrays but copies
empty host ranges. As a result, requested SoA real values are not associated
with particles and the newly grown destination entries remain uninitialized.

## Proposed patch

Replace size-comparison sentinels with explicit counts and stage one value per
particle for every requested SoA component. Assert that all staged vectors
remain the same length as the particle vector, and copy exactly that length in
both redistribution branches.

Add ASCII import tests with one and several compile-time SoA real components,
with and without replication and with enough readers to exercise both batch
paths.

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

## Verification

**Conclusion: Confirmed.**

`nreals` is resized to exactly `extradata - NStructReal`, but every push/pop
guard requires its size to be greater than that same count. The first copy
path also requires a fixed `NArrayReal` array's size to be greater than
`NArrayReal`. These conditions are false, so requested compile-time SoA values
never accompany the particles.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This issue requires `NArrayReal > 0` and an ASCII file whose `extradata`
extends past the AoS struct fields into compile-time SoA arrays. Quokka's
particle aliases specify only the first two `AmrParticleContainer` template
arguments, which are AoS real and integer counts; their compile-time SoA real
count is zero. All values read by Quokka's ASCII initializers are consequently
written through `Particle::rdata`, not the broken `nreals` staging vectors.

No Quokka container can satisfy the required template layout without changing
its particle type definitions. The faulty conditions remain in AMReX but are
not instantiated for Quokka's ASCII imports, so the issue definitely does not
affect current Quokka.

# Particle level mutators accept negative level indices

## Severity

High

## Affected code

`RemoveParticlesAtLevel`, both `CreateVirtualParticles` implementations, both
`CreateGhostParticles` implementations, and `AddParticlesAtLevel` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

These public entry points either check only an upper bound or rely on debug
assertions that do not consistently include `level >= 0`. In optimized builds,
a negative level passes conditions such as `level >= m_particles.size()` and
is then used to index a vector or construct a particle iterator.

The result is out-of-bounds access or undefined iterator behavior rather than
a clear API diagnostic. `RemoveParticlesAtLevel(-1)` is the simplest example:
its only guard is the upper-bound comparison before `m_particles[level]`.

## Proposed patch

Give every public level-taking mutator an always-on range check before any
indexing. For routines that intentionally allow a not-yet-materialized level,
still require `level >= 0` and validate it against the grid database.

Add optimized-build death tests for `-1` on each entry point and ordinary
boundary tests for level zero and the finest valid level.

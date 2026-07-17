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

## Verification

**Conclusion: Confirmed.**

The listed public paths lack an always-on lower-bound check. For example,
`RemoveParticlesAtLevel()` returns only for `level >= ssize(m_particles)`, so
`-1` passes and directly indexes `m_particles[-1]`; the virtual, ghost, and add
paths similarly rely on incomplete debug assertions or upper bounds.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka does not call the listed public level mutators:
`RemoveParticlesAtLevel`, `CreateVirtualParticles`, `CreateGhostParticles`, or
`AddParticlesAtLevel`. Particle deletion marks IDs invalid and relies on
standard redistribution; creation writes through
`DefineAndReturnParticleTile`; AMR level changes use container redistribution.
No negative level is forwarded to these affected methods.

Because the defect is method-specific rather than a shared level-index helper,
Quokka's other valid-level loops do not enter it. Adding one of these mutator
calls would be required, so current non-impact is definite.

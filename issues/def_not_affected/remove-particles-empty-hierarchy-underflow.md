# Removing non-finest particles underflows on an empty hierarchy

## Severity

High

## Affected code

`ParticleContainer_impl::RemoveParticlesNotAtFinestLevel` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

The default particle-container constructor intentionally creates a container
with no level hierarchy, so `m_particles` can be empty. The removal routine
iterates with an unsigned level variable and the bound
`m_particles.size() - 1`.

For an empty vector, that subtraction wraps to the maximum unsigned value and
the first loop iteration indexes `m_particles[0]`. The preceding assertion
does not protect this case: an undefined hierarchy can have `finestLevel() ==
-1`, making `finestLevel()+1 == m_particles.size()` true.

## Proposed patch

Return immediately when `m_particles.size() <= 1`, or express the loop using a
signed level count after checking for an empty hierarchy. Add regressions for
default-constructed, one-level, and multi-level containers.

## Verification

**Conclusion: Confirmed.**

The loop uses an unsigned level and bound `m_particles.size() - 1`. A default
container has no particle levels; the subtraction wraps and the loop indexes
level zero. Its preceding assertion still succeeds because both
`finestLevel()+1` and the vector size are zero.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The unsigned underflow is contained in
`RemoveParticlesNotAtFinestLevel()` and requires calling it on a default,
undefined particle container whose level vector is empty. Quokka has no call
to that removal routine. Its production particle containers are attached to
the simulation hierarchy, while temporary analysis containers are explicitly
`Define`d with a level-zero hierarchy before use.

Thus Quokka supplies neither half of the trigger: it does not invoke the
affected API, and its live containers are not left with zero particle levels.
The faulty `size() - 1` loop bound is consequently unreachable in current
Quokka execution.

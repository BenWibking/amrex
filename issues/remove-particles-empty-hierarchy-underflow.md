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

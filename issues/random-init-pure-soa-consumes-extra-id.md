# Parallel pure-SoA random initialization consumes two IDs per particle

## Severity

Medium

## Affected code

The `serialize == false` pure-SoA branch of
`ParticleContainer_impl::InitRandom` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

The parallel loop first assigns `ptest.id() = ParticleType::NextID()`. The AoS
branch reuses that ID, but the pure-SoA branch ignores it and calls
`ParticleType::NextID()` again when constructing packed ID/CPU storage.

Every generated pure-SoA particle therefore consumes two IDs and retains only
the second. ID progression differs between serialized and parallel modes and
reaches the ID limit twice as quickly in this path.

## Proposed patch

Store the ID generated for `ptest` into the pure-SoA packed ID/CPU value, or
delay ID generation until the layout-specific branch and call `NextID()` once.

Add a next-ID progression test comparing AoS and pure-SoA initialization in
both serialization modes.

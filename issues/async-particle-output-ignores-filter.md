# Async particle output ignores the caller's filter

## Severity

High

## Affected code

`ParticleContainer_impl::WriteBinaryParticleData` in
`Src/Particle/AMReX_ParticleIO.H` and `WriteBinaryParticleDataAsync` in
`Src/Particle/AMReX_WriteBinaryParticleData.H`.

## Explanation

The public templated writer accepts a predicate `f` and the synchronous path
passes it through to flag generation. When async output is enabled, the
dispatcher calls `WriteBinaryParticleDataAsync` without `f`; that implementation
hard-codes `KeepValidFilter` instead.

Every valid particle is therefore written regardless of the predicate. The
same source call produces different particle counts and data solely because
`AsyncOut::UseAsyncOut()` changed, with no diagnostic that filtered async
output is unsupported.

## Proposed patch

Template the async writer on the predicate and use it while staging the pinned
tiles and computing per-grid counts. Ensure the captured/staged state does not
depend on the predicate's lifetime. If arbitrary predicates cannot be made
async-safe, reject that overload while async output is active.

Add synchronous/async parity tests with a predicate that selects a distinctive
subset of valid particles.

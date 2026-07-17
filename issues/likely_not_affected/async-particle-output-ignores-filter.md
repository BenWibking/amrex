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

## Verification

**Conclusion: Confirmed.**

`WriteBinaryParticleData()` forwards predicate `f` only to the synchronous
writer. Its async call has no predicate parameter, and the implementation
counts and stages exclusively with valid-ID checks and `KeepValidFilter`.
Custom selection is therefore lost whenever async output is enabled.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka's production particle plotfile and checkpoint wrappers request all
components and rely on the normal valid-particle selection; they do not pass a
custom particle predicate. Quokka also rejects asynchronous output and ships
no input enabling it. Thus existing Quokka calls neither select the affected
async dispatcher nor ask it to preserve a custom filter, and the default
valid-ID behavior matches what those calls require.

This remains short of definitely unaffected because the AMReX filtering
overload is available to Quokka problem code and the async parameter is still
parsed below Quokka. A downstream diagnostic could supply a predicate, or an
unsupported initial checkpoint could run before Quokka's async guard. No such
combination exists in the current tree, so impact is unlikely rather than
demonstrated or impossible.

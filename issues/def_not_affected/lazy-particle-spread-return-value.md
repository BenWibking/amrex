# Lazy particle spread functions return before their reductions run

## Severity

Low

## Affected code

`ParticleContainer_impl::ByteSpread` and `PrintCapacity` in
`Src/Particle/AMReX_ParticleContainerI.H` when `AMREX_LAZY` is enabled.

## Explanation

Both functions queue the min/max/sum reductions in a lambda that captures the
local counters by value, then immediately return an array formed from the
original local variables. The queued mutable copies are reduced later only for
printing; they cannot update the already returned values.

Thus the public return value is a local triple rather than the documented-by-
behavior global spread whenever lazy reductions are enabled. Callers that use
the returned array receive build-configuration-dependent results.

## Proposed patch

Do not defer a reduction whose values are returned synchronously. Perform the
reductions before returning, or change the API to a deliberately asynchronous
result/callback and make the print-only helper separate.

Add a multi-rank `AMREX_LAZY` regression that consumes the returned values,
not merely the printed line.

## Verification

**Conclusion: Confirmed.**

Under `AMREX_LAZY`, both methods capture `cnt`, `mn`, and `mx` by value in a
queued mutable lambda, then immediately form their return array from the
unreduced local variables. The later reductions can affect only the printed
copies and cannot change the synchronous return value.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The bad return values are observable only through `ByteSpread()` and
`PrintCapacity()` in a build using `AMREX_LAZY`. Quokka has no call to either
particle diagnostic; its performance hints, statistics, and I/O code compute
their own counts and byte/file metrics. Neither result participates internally
in redistribution or checkpointing.

Even if a Quokka dependency were compiled with `AMREX_LAZY`, the two methods
would remain uncalled. A new diagnostic call is required to observe the bug,
so current Quokka is definitely unaffected.

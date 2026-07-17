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

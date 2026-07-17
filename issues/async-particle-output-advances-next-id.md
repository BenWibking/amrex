# Async particle output consumes a particle ID

## Severity

Medium

## Affected code

The `maxnextid` calculation in `WriteBinaryParticleDataAsync` in
`Src/Particle/AMReX_WriteBinaryParticleData.H`.

## Explanation

`ParticleType::NextID()` is not a passive getter: it returns the current ID and
increments the static counter. The synchronous writer compensates immediately
with `ParticleType::NextID(maxnextid)` before reducing the header value.

The async writer calls `ParticleType::NextID()` but never restores the counter.
Every async particle output therefore consumes one ID on every participating
rank even though no particle was created. Output frequency changes the future
ID sequence, and a long-running job can reach the ID limit earlier solely due
to plotfile/checkpoint writes.

## Proposed patch

Restore the local counter immediately after reading it, matching the
synchronous path, then perform the maximum reduction for the header. A cleaner
long-term fix would add a non-mutating accessor for the next-ID state and use
it in both writers.

Add a regression that records the next ID, performs async output, creates one
particle, and verifies that its ID is exactly the value that was current before
the write.

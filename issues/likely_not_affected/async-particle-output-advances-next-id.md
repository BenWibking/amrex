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

## Verification

**Conclusion: Confirmed.**

The async writer obtains `maxnextid` by calling mutating `NextID()` and reduces
it without restoring the local counter. The synchronous implementation calls
the same getter and immediately invokes the setter with the captured value,
showing the required compensation that the async path omits.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka uses AMReX's particle IDs and writes particle checkpoints, but its
supported output path is synchronous. It explicitly aborts when
`amrex.async_out` is enabled, and no checked-in input enables that option.
Consequently normal Quokka output takes the synchronous code that restores
`NextID()` after reading it, so checkpointing does not consume an ID through
this bug.

The affected async dispatcher still exists in the AMReX dependency and Quokka
does not remove the parameter. In addition, a fresh Quokka run can write an
initial checkpoint before the later async-output guard executes. An
unsupported run combining async output, particles, and an initial checkpoint
could therefore advance the counter before aborting. That residual reachable
configuration supports likely-not-affected rather than a categorical
definitely-not-affected classification.

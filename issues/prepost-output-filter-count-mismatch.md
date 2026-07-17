# Pre/post output caches the wrong total for custom filters

## Severity

Medium

## Affected code

`CheckpointPre` and the filtered `WritePlotFile`/`WriteBinaryParticleData`
paths in `Src/Particle/AMReX_ParticleIO.H` and the pre/post branch of
`WriteBinaryParticleDataSync`.

## Explanation

`CheckpointPre` caches the count of every valid particle. When pre/post mode is
enabled, the synchronous writer uses that cached value for the global header
instead of counting the flags generated from its actual predicate `f`.

For a custom plotfile filter that selects only a subset of valid particles,
per-grid counts and records follow `f`, but the header total reports all valid
particles. The same API therefore writes internally inconsistent metadata only
when `particles.use_prepost` is enabled.

## Proposed patch

Do not reuse the valid-particle cache for arbitrary filtered output. Either
cache predicate-specific flags/counts as part of a paired pre operation, or
fall back to the ordinary flag reduction whenever the filter is not the exact
valid-ID predicate.

Add pre/post on/off parity tests using a filter that selects a known fraction
of particles and compare the header total to summed grid counts.

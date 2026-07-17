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

## Verification

**Conclusion: Confirmed.**

Filtered synchronous output builds per-particle flags from the supplied
predicate, but with pre/post enabled the header total comes from
`GetNParticlesPrePost()`. `CheckpointPre()` populates that cache by counting all
valid AoS particles and knows nothing about the output predicate, so a
selective filter makes the header total disagree with the emitted grid counts.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Two independent nondefault conditions are needed here. Quokka leaves
`particles.use_prepost` false, and its production particle checkpoint and
plotfile wrappers do not supply a custom per-particle predicate; they write all
valid particles. With no selective filter, the pre-pass valid-particle total
and the writer's valid-particle records describe the same set even if pre/post
mode were enabled. Under current defaults `CheckpointPre` returns immediately
as well.

Both capabilities remain available through AMReX to out-of-tree Quokka
diagnostics. A future filtered particle plotfile combined with pre/post output
would reach the mismatch without further library changes. The absence of that
combination today makes impact unlikely, while the directly exposed options
prevent a definite exclusion.

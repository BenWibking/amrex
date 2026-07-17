# Filter-and-transform overruns source ranges for nonzero src_start

## Severity

High

## Affected code

The single-destination `filterAndTransformParticles` overloads that accept
`src_start` in `Src/Particle/AMReX_ParticleTransformation.H`.

## Explanation

The API documentation says `src_start` is the source offset at which reading
begins. The implementation nevertheless sets `np = src.numParticles()` and
launches `np` iterations, passing `src_start + i` to the predicate and
transform. For any positive `src_start`, the final `src_start` iterations refer
past the end of the source tile.

Current test code avoids the fault by subtracting `src_start` inside custom
callbacks and explicitly rejecting the tail. That workaround reverses the
documented meaning of the argument and does not make ordinary transforms that
index `src[src_i]` safe.

The mask overload likewise scans `src.numParticles()` mask entries rather than
the number of particles remaining from `src_start`.

## Proposed patch

Define the processed count as `src.numParticles() - src_start`, validate that
`src_start` is in range, size/scan the mask for that count, and pass actual
source indices `src_start + i` to callbacks. If callers need an arbitrary
logical index bias, expose it as a separate argument rather than overloading a
source-range offset.

Replace the workaround test with a direct transform that indexes
`src[src_i]`, and cover `src_start` values zero, an interior offset, exactly the
source size, and beyond the source size.

## Verification

**Conclusion: Confirmed.**

The offset overloads set the work count to the source's full particle count
instead of the remaining `numParticles() - src_start`. A positive source
offset therefore makes predicate and transform indices run past the source.
The existing transformation test explicitly shortens the source as a
workaround and comments on this internal overrun.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka has no direct call to `filterAndTransformParticles`. The AMReX virtual-
and ghost-particle implementations are the main particle-container clients,
but Quokka calls neither of those APIs. Even those internal calls shown in the
current library pass `src_start == 0`, whereas this report requires a positive
source offset.

No Quokka-owned source range can therefore reach the overrun. Introducing a
new offset transformation feature would be necessary to instantiate the
failing case, so current Quokka is definitely unaffected.

# Random initialization can loop forever on a zero-width containing box

## Severity

High

## Affected code

`ParticleContainer_impl::InitRandom` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

The supplied `RealBox` is accepted when it is geometrically "ok" and contained
in the domain, even if one active direction has equal lower and upper bounds.
The rejection sampler then requires `x >= xlo` and `x < xhi`. No value can
satisfy both for a zero-width interval, so particle generation loops forever.

## Proposed patch

Before sampling, require `xhi[d] > xlo[d]` for every active direction. Reject
empty or degenerate boxes with an always-on message instead of silently
resetting valid-looking input.

Add zero-width, negative-width, out-of-domain, and valid sub-box tests.

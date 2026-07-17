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

## Verification

**Conclusion: Confirmed.**

`RealBox::ok()` explicitly accepts nonnegative lengths, including zero, and a
degenerate box can be contained by the problem domain. `InitRandom()` then
uses a rejection condition requiring a sample to be both at least the lower
bound and strictly below the identical upper bound, so the loop cannot
terminate.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

The only checked-in Quokka call to the affected `ParticleContainer::InitRandom`
is the spherical-collapse particle setup. It samples the simulation's normal
three-dimensional problem domain and does not pass a custom degenerate
`RealBox`. Quokka's mesh initialization requires a positive cell count and a
physical domain with positive extent in every active direction, so the default
containing box has nonzero volume and the rejection sampler can terminate.

The affected overload is still directly used and permits a caller-provided
containing box. A future problem specialization could pass a zero-width box
that lies inside the domain, and Quokka has no additional validation layer.
Current domain-derived arguments avoid the trigger, supporting likely rather
than definitely not affected.

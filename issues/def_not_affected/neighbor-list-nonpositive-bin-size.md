# Custom neighbor bins accept nonpositive sizes

## Severity

High

## Affected code

Both custom-`bin_size` `buildNeighborList` overloads in
`Src/Particle/AMReX_NeighborParticlesI.H`.

## Explanation

The routines compute each bin count as `ceil(physical_extent / bin_size)` but
never require `bin_size` to be finite and positive. A zero value produces an
infinite floating result whose conversion to `int` is undefined. A negative
value is forced to one bin by `std::max`, silently violating the stated search
resolution. NaN similarly propagates into an invalid integer conversion.

This can lead to invalid allocations, division by zero, or an incomplete
neighbor search before the user callback is evaluated.

## Proposed patch

Add an always-on `std::isfinite(bin_size) && bin_size > 0` precondition at the
entry to both overloads. Check the computed per-axis bin counts and their
product for representability before allocating.

Add tests rejecting zero, negative, NaN, and infinity, plus a positive-size
test for both self and cross-container builds.

## Verification

**Conclusion: Confirmed.**

Both custom-bin overloads divide a physical extent by the caller's
`bin_size` and convert the `ceil` result to `int` without validating the
value. Zero, NaN, and infinity make that conversion invalid, while a negative
size is silently clamped to one bin and does not implement the requested
search resolution.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The affected custom-bin-size overloads build neighbor lists for
`NeighborParticleContainer`. Quokka has no neighbor container, no
`buildNeighborList` call, and no particle input parameter that is forwarded as
this bin size. Quokka's mesh cell sizes and particle deposition widths are
handled by separate interpolation code.

Thus zero, negative, or nonfinite values in a Quokka problem cannot enter this
conversion. Integrating AMReX neighbor lists would be prerequisite work, which
makes current non-impact definite.

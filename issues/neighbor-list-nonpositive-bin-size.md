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

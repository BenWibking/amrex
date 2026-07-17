# ParticleContainerBase constructor discards anisotropic refinement ratios

## Severity

Medium

## Affected code

The `ParticleContainerBase` constructor accepting `Vector<IntVect> rr` in
`Src/Particle/AMReX_ParticleContainerBase.H`.

## Explanation

`ParGDB` natively accepts `Vector<IntVect>` and preserves a refinement ratio
for every coordinate direction. This `ParticleContainerBase` constructor
instead converts each `IntVect` to the scalar `iv[0]`, constructs a
`Vector<int>`, and passes that to `ParGDB`.

Debug assertions require the other components to equal `iv[0]`, but those
assertions disappear in optimized builds. An input such as `(2, 4, 4)` is then
silently stored as `(2, 2, 2)`. Particle level assignment, neighbor extents,
and cross-level coordinate transformations use the wrong ratios. The matching
`Define` and `SetParGDB` overloads already pass `Vector<IntVect>` through
without this loss.

## Proposed patch

Construct `ParGDB` directly with the supplied `Vector<IntVect>`, matching the
other base-class entry points. If anisotropic particle hierarchies are meant to
be rejected, use an always-on diagnostic consistently in `ParGDB` rather than
silently changing the values in release builds.

Add a constructor test with unequal directional ratios and verify that
`refRatio(level)` returns the original `IntVect`.

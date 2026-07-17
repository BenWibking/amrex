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

## Verification

**Conclusion: Confirmed.**

The `Vector<IntVect>` constructor converts every ratio to `iv[0]` and calls
the scalar-ratio `ParGDB` constructor. Debug-only assertions reject unequal
components, but optimized builds silently replace anisotropic input such as
`(2,4,4)` with `(2,2,2)`. The matching `Define` and `SetParGDB` paths preserve
the `IntVect`s.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The lossy code is one standalone `ParticleContainerBase` constructor taking
geometry, box arrays, maps, and `Vector<IntVect>` ratios. Quokka production
containers instead use the constructor taking the simulation `AmrCore`, whose
`AmrParGDB` preserves directional ratios. Temporary analysis containers use a
single-level `Define` call, and restart refinement uses `SetParGDB`/`Define`,
which this issue notes are also correct.

Quokka therefore never invokes the converting constructor, even when its AMR
hierarchy has anisotropic ratios. Constructor overload resolution structurally
excludes the bug, so current impact is definitely absent.

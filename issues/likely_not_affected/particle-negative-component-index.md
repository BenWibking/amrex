# Particle component accessors accept negative indices

## Severity

Medium

## Affected code

The indexed `Particle::pos`, `Particle::rdata`, `Particle::rvec`, and
`Particle::idata` overloads in `Src/Particle/AMReX_Particle.H`.

## Explanation

The accessors assert only that an index is less than the component count. A
negative integer satisfies every such upper-bound check and is then used to
index a C array before its first element. The `IntVect` form of `rvec` similarly
uses only `allLT(NReal)`, which also accepts negative components.

In assertion-enabled builds the invalid access is not caught; in release builds
it is likewise undefined behavior and can read or overwrite adjacent particle
fields such as positions or packed identity data.

## Proposed patch

Check both bounds in every scalar accessor (`index >= 0 && index < count`) and
use both `allGE(0)` and `allLT(NReal)` for vector component selections. Prefer
an always-on diagnostic if these public accessors are expected to defend
against runtime component values in optimized builds.

Add assertion/death tests for `-1` on every accessor family plus boundary tests
for zero and the last valid component.

## Verification

**Conclusion: Confirmed.**

The `pos`, `rdata`, `idata`, and related vector accessors assert only an upper
bound. A negative signed component index passes those assertions and indexes
before the particle storage, defeating the intended debug-time bounds check.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka accesses particle data through compile-time enum indices such as
`CICParticleMassIdx`, `SinkParticleMdotIdx`, and the generated radiation-group
offsets. Optional component queries can return `-1`, but the particle
descriptor methods check those results before calling `rdata` or `idata`; for
example, mass, luminosity, birth-time, and evolution-stage operations are
guarded by capability tests. Position loops also run from zero to
`AMREX_SPACEDIM - 1`. No checked-in access deliberately forwards a negative
component index.

The API is heavily used and some indices are selected dynamically by particle
type, so a missed guard in future physics code could expose the faulty AMReX
assertion. The current enum and capability discipline makes impact unlikely,
but not sufficiently impossible for the definite category.

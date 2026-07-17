# SoA ParticleArray silently ignores assignment from lvalue components

## Severity

Medium

## Affected code

`ref_wrapper` in `Src/Particle/AMReX_ParticleArray.H`.

## Explanation

The proxy defines assignment to the referenced value only for `T&&`. An
expression such as `particle.x() = 7.0` therefore works, but assignment from an
lvalue of the same type does not select that overload:

```cpp
double value = 7.0;
particle.x() = value;
```

Because `ref_wrapper(T&)` is implicit and copy assignment is defaulted, the
lvalue can instead be converted to a temporary `ref_wrapper` and the proxy's
pointer is rebound. The proxy is a member of a temporary particle view, so that
rebind disappears immediately and the underlying SoA component remains
unchanged. The statement compiles without a diagnostic while failing to perform
the apparent particle update.

## Proposed patch

Add an `AMREX_GPU_HOST_DEVICE` assignment overload taking `T const&` that writes
through `get()`, alongside the rvalue overload. Consider making the converting
constructor explicit or defining wrapper-to-wrapper assignment semantics
deliberately so value assignment cannot fall through to a temporary rebind.

Extend `Tests/Particles/ParticleArray` to assign component values from both
lvalues and rvalues on AoS and SoA layouts, then read them back in a separate
kernel.

## Verification

**Conclusion: Confirmed.**

`ref_wrapper` provides write-through assignment only as `operator=(T&&)`.
For an lvalue, overload resolution can instead use the wrapper's copy
assignment after implicitly constructing a temporary wrapper, rebinding that
temporary rather than modifying the referenced component. Existing tests
exercise only rvalue literals and do not cover this path.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The `ref_wrapper` assignment overload is part of the SoA `ParticleArray`
proxy. Quokka does not use that particle layout; its GPU kernels obtain an AoS
`Particle&` or tile data and assign fields through `pos`, `rdata`, and `idata`.
Those setters write actual AoS members and do not use this wrapper's overload
resolution.

No Quokka lvalue component assignment can therefore bind to the faulty
wrapper. A pure-SoA conversion would be required first, making the exclusion
definite for current Quokka.

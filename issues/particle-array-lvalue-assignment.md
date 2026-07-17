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


# Tracer timestamping accepts out-of-range component indices

## Severity

Medium

## Affected code

`TracerParticleContainer::Timestamp` in
`Src/Particle/AMReX_TracerParticles.cpp`.

## Explanation

The public `indices` argument is used directly as `vals[indices[i]]` after
`vals` is sized to `mf.nComp()`. There is no validation that each requested
component satisfies `0 <= index < mf.nComp()`.

A negative or too-large component index therefore causes an out-of-bounds host
read while formatting the timestamp file. Depending on the value and build,
this can crash, disclose unrelated memory in output, or silently write
incorrect data.

## Proposed patch

Validate every requested component once at function entry with an
`AMREX_ALWAYS_ASSERT_WITH_MESSAGE` (or issue a descriptive `amrex::Error`) so
release builds fail before opening output files. Include the invalid value and
`mf.nComp()` in the diagnostic.

Add unit coverage for the lowest and highest valid indices and a death test for
`-1` and `mf.nComp()`.

## Verification

**Conclusion: Confirmed.**

`TracerParticleContainer::Timestamp` allocates interpolation results with
`mf.nComp()` entries and later writes `vals[indices[i]]` without validating any
requested index. Negative indices and indices at or above `mf.nComp()`
therefore cause an out-of-bounds access.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka does instantiate `AmrTracerParticleContainer`, so the class name alone
would suggest possible exposure. Its tracer call sites use advection,
redistribution, plotfile output, and checkpoint/restart operations, however;
there is no call to `TracerParticleContainer::Timestamp()` and no timestamp
component-index list is constructed.

The unchecked `indices[i]` access exists only inside that unused method.
Because none of Quokka's tracer operations delegate to `Timestamp()`, even a
Quokka `MultiFab` with many or few components cannot exercise the invalid
indexing path.

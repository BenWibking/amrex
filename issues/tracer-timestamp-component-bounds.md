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


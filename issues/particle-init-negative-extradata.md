# File initializers accept a negative extradata count

## Severity

High

## Affected code

`InitFromAsciiFile` and `InitFromBinaryFile` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

Both routines check only an upper bound, and only with debug assertions at
entry. A negative `extradata` therefore reaches optimized builds. In the
binary reader it changes the ignored-field count to `NX - extradata`, causing
each iteration to consume more values than one file record contains and
misalign every subsequent particle read. In the ASCII reader it silently
changes the requested schema instead of reporting invalid input.

## Proposed patch

Require `extradata >= 0` and the layout-specific upper bound with an always-on
check before opening the file or allocating buffers. Keep file-header `NX`
validation separate from the caller-request validation.

Add negative-count tests for both readers and verify that no particle or ID
state changes after rejection.

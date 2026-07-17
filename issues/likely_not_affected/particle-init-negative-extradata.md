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

## Verification

**Conclusion: Confirmed.**

Both file readers assert only an upper bound on `extradata`. In an optimized
binary build a negative value makes `NX - extradata` larger than the physical
record's extra-field count, so each iteration consumes bytes from later
records. The ASCII path also accepts the invalid schema silently.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka invokes `InitFromAsciiFile` with local constants that describe each
particle schema: four for CIC/sink prefixes, complete group-dependent counts
for radiating particles, and other explicitly positive counts for test and
stellar particles. It does not call `InitFromBinaryFile`, and no input
parameter is converted into the `extradata` argument. Every checked-in call
therefore supplies a nonnegative value and avoids the invalid record-size
calculation.

The affected ASCII API is still directly used throughout Quokka problem code,
so a future specialization could compute or forward a bad count. AMReX would
not reject it in optimized builds. Current arguments are compile-time or local
positive constants, which makes impact unlikely but not an architectural
impossibility.

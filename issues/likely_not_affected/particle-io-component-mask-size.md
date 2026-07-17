# Particle I/O does not validate component-mask lengths

## Severity

High

## Affected code

The mask-taking `Checkpoint`/`WritePlotFile` overloads in
`Src/Particle/AMReX_ParticleIO.H` and both binary writers in
`Src/Particle/AMReX_WriteBinaryParticleData.H`.

## Explanation

Some wrappers check mask lengths only with debug `AMREX_ASSERT`, while the
lowest-level writers validate component-name lengths but never validate
`write_real_comp` or `write_int_comp`. Packing, header generation, and file-size
calculation then index those masks using the full compile-time/runtime
component count.

In optimized builds, a short mask causes out-of-bounds reads and can make the
header's component count disagree with the packed record size. An oversized
mask can likewise make names and selection semantics diverge.

## Proposed patch

At the public binary-writer boundary, always require exact mask lengths for
the layout, using the same pure-SoA position exclusion as the name checks.
Validate before creating directories or mutating output state, and make wrapper
checks consistently always-on.

Add optimized-build tests for short and long real/int masks plus a valid
pure-SoA mask.

## Verification

**Conclusion: Confirmed.**

The particle I/O entry points in `Src/Particle/AMReX_ParticleIO.H` do not
consistently enforce exact real and integer mask lengths: some wrapper checks
are debug-only and the raw mask-and-name overload forwards without validating
the masks. The writers subsequently index each mask across the full component
range, so undersized masks can be read out of bounds in release builds.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka does not build arbitrary component masks. Its plotfile and checkpoint
wrappers pass complete component-name vectors to AMReX overloads that generate
all-enabled masks sized from the container's own compile-time component
counts. Tracer output uses the default all-component overload. Consequently
the masks reaching the low-level writer have one entry for every real and
integer component, and the missing validation is not exercised by current
Quokka output.

The raw mask overload remains available to Quokka diagnostics and future
filtered-output work, and the named wrapper ultimately traverses the same I/O
implementation. A future selective component policy could construct an
incorrect vector and receive no release-mode protection. Current wrappers
make that unlikely, but the shared API surface prevents a definite exclusion.

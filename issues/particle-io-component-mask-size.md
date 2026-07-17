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

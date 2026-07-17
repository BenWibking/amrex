# InitRandomPerBox generates icount cubed particles per box

## Severity

Medium

## Affected code

`ParticleContainer_impl::InitRandomPerBox` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

The public documentation says `icount` is the number of randomly distributed
particles per box. The implementation nests three loops, each running
`icount_per_box` times, unconditionally in every dimensional build. It thus
creates `icount_per_box^3` particles per box, including in 1D and 2D.

This can exceed the requested count by orders of magnitude and makes the count
dimensionally inconsistent. Existing call sites that interpret the argument
as points per direction rely on behavior that the API does not state.

## Proposed patch

Resolve the contract explicitly. For a true particle count, use one loop and
sample each active coordinate. If the routine is meant to create a jittered
tensor product, rename the parameter/API, use dimension-conditional loops, and
document the resulting `icount^AMREX_SPACEDIM` count.

Add exact-count tests in every supported dimension.

## Verification

**Conclusion: Confirmed.**

The public declaration says `icount` randomly distributed particles per box.
The implementation executes three unconditional nested loops, each of length
`icount_per_box`, even in 1D and 2D builds, and therefore appends
`icount_per_box^3` records per box.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The erroneous cubic loop nest belongs specifically to
`ParticleContainer::InitRandomPerBox()`. Quokka has no call to that initializer;
its particle setup paths use inputs such as `InitRandom()`, one-particle-per-cell
initialization, or problem-specific insertion instead.

This is a call-site exclusion rather than an assumption about the requested
count or spatial dimension. Since Quokka never enters `InitRandomPerBox()`, it
cannot interpret a per-box count through the faulty three-loop implementation
in any supported build dimension.

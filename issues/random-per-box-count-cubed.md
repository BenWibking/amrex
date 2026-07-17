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

# InitOnePerCell accepts an upper-face offset that loses boundary particles

## Severity

Medium

## Affected code

`ParticleContainer_impl::InitOnePerCell` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

The function comment and assertions allow offsets in the closed interval
`[0,1]`, but it also asserts each generated position is strictly below the grid
upper bound. With an offset of exactly one, the last cell in a grid lands on
that upper face. At an internal face it belongs to another cell/grid; at a
nonperiodic problem boundary it lies outside the domain and is invalidated by
the final redistribution.

Debug builds can fail before redistribution, while optimized builds can return
fewer than one particle per cell.

## Proposed patch

Make the supported interval `[0,1)` and enforce it with always-on checks, or
define an explicit face-placement ownership rule that shifts upper-face
particles consistently and handles the physical boundary.

Add tests for offsets zero, one-half, exactly one, and values just below one.

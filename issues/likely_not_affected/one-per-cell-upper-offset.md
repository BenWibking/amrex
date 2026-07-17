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

## Verification

**Conclusion: Confirmed.**

The documented and asserted interval includes one, but the generated position
for a grid's last cell is then exactly `grid_box.hi()`. A following assertion
requires every coordinate to be strictly below that bound; without assertions,
the final redistribution can transfer an internal-face particle or invalidate
a problem-boundary particle.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka directly uses `InitOnePerCell` to create tracer particles, but the call
in `AMRSimulation::InitParticles` passes `(0.5, 0.5, 0.5)`. Those offsets place
each tracer at the cell center, strictly inside both the grid box and physical
domain, so the high-side `offset == 1` failure cannot occur in the checked-in
tracer workflow. No other Quokka call uses this initializer or obtains its
offsets from an input deck.

The method remains part of Quokka's public problem-development surface, and a
future tracer policy could choose a face-aligned offset that AMReX documents
as allowed. Because Quokka invokes the exact affected initializer but its
current constant arguments avoid the boundary case, likely not affected is
more accurate than either definite category.

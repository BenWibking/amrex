# Periodic particle location ignores the container's CellAssignor

## Severity

High

## Affected code

`ParticleContainer_impl::EnforcePeriodicWhere` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

Normal location calls `Index` using the container's `CellAssignor` template
parameter. After a periodic shift, however, `EnforcePeriodicWhere` explicitly
instantiates both index calculations with `DefaultAssignor`.

A particle container with a custom cell-assignment policy can therefore map a
particle to one grid before a periodic crossing and use a different policy
after the crossing. The returned level, grid, tile, and cell can disagree with
`Where`, and subsequent redistribution or deposition can place the particle
incorrectly.

## Proposed patch

Use `CellAssignor` for the shifted and fallback `Index` calls, matching the
non-periodic path. If periodic location is intentionally unsupported for
custom assignors, reject that combination explicitly instead of silently
switching policies.

Add a periodic regression with a deliberately distinct custom assignor and
verify that `Where` and `EnforcePeriodicWhere` agree after wrapping.

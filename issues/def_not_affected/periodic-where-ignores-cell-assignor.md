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

## Verification

**Conclusion: Confirmed.**

`EnforcePeriodicWhere()` explicitly instantiates both shifted-particle index
calls with `DefaultAssignor`, while the ordinary `Where` path uses the
container's `CellAssignor`. A periodic crossing can therefore change the
location policy for a valid custom-assignor container.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka uses the default AMReX `CellAssignor` for every tracer and physics
particle container. It does not provide a custom assignor template argument.
Thus the hard-coded `DefaultAssignor` in `EnforcePeriodicWhere` is the same
policy the container would otherwise invoke, and periodic crossings cannot
change Quokka's location semantics through this issue.

Although Quokka uses periodic particle redistribution, the defect is precisely
the loss of a nondefault policy that Quokka does not have. A new custom
assignor type would be required to create any behavioral difference, so the
current non-impact is definite.

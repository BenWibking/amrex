# GPU neighbor communication always uses a one-cell rank set

## Severity

High

## Affected code

Calls to `ParticleCopyPlan::build` from `fillNeighborsGPU` and
`updateNeighborsGPU` in `Src/Particle/AMReX_NeighborParticlesGPUImpl.H`.

## Explanation

`ParticleCopyPlan::build` interprets its final integer as the local
communication grow width and calls `pc.NeighborProcs(local)`. The GPU neighbor
path passes the boolean literal `true`, which becomes the integer one, even
when `m_num_neighbor_cells` is larger.

`buildNeighborMask` and `buildNeighborCopyOp` can generate destination copies
out to the configured multi-cell halo. During MPI plan construction, however,
only buckets owned by ranks found with a one-cell grow are visited. A
destination rank reachable only by the wider halo receives no metadata or
particle data, silently dropping required neighbor copies.

## Proposed patch

Pass `m_num_neighbor_cells` as the local grow width when building both the full
and boundary-only copy plans. Clarify the plan argument name/type so it is not
mistaken for a boolean, and require a positive value when local communication
is selected.

Add a multi-rank GPU test with small adjacent grids and a neighbor width
greater than one that reaches a rank outside the one-cell process set.

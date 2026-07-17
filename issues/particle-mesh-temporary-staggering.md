# Particle-mesh remapping loses the MultiFab index type

## Severity

High

## Affected code

`ParticleToMesh` and `MeshToParticle` in
`Src/Particle/AMReX_ParticleMesh.H`.

## Explanation

When the particle and mesh layouts are not the same, both functions allocate a
temporary `MF` directly on `pc.ParticleBoxArray(lev)`. Particle box arrays are
cell-centered. The code does not convert that array to `mf.ixType()`.

For nodal or face-centered `MultiFab`s, the temporary therefore has a different
staggering from the supplied mesh. Particle callbacks index it according to the
intended mesh centering, which can omit high-side nodes/faces or write outside a
temporary FAB. The subsequent `ParallelAdd`/`ParallelCopy` also transfers
between incompatible index spaces, producing missing or shifted values.
The host `ParticleToMesh` path compounds this by sizing `local_fab` from the
cell-centered particle tile box rather than a box converted to the mesh index
type.

## Proposed patch

Construct remapping temporaries on
`amrex::convert(pc.ParticleBoxArray(lev), mf.ixType())`, and convert each host
tile box to the same index type before allocating the tile-local FAB. Audit the
grow-cell and boundary-sum regions for nodal overlap semantics so shared nodes
are accumulated exactly once.

Add mismatched-layout tests for cell-, node-, and each face-centered index type
with different `BoxArray`/`DistributionMapping` layouts, covering both deposit
and gather paths.


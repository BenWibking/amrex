# Cell virtual aggregation allocates the global cell count on every rank

## Severity

Medium

## Affected code

The `virts.resize(imf.sum(0))` call in the `Cell` branch of
`ParticleContainer_impl::CreateVirtualParticles` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

`iMultiFab::sum` defaults to `local = false`, so this call performs a global
reduction and returns the global occupancy sum on every rank. Each output tile
contains only the calling rank's virtual particles, and the code later shrinks
it to a locally computed offset. The initial resize therefore allocates space
for the global particle count on every rank solely as transient storage.

At scale this changes aggregate memory from proportional to the global count
to roughly the global count times the MPI rank count and can cause avoidable
out-of-memory failures. It also introduces an unnecessary collective.

## Proposed patch

Request the local sum explicitly (`local = true`) or compute the local occupied
cell count while producing the offsets. Use a size type that matches the tile
API and retain a separate global reduction only if a diagnostic needs it.

Add a multi-rank regression that checks each rank's initial/final allocation
against its local occupancy.

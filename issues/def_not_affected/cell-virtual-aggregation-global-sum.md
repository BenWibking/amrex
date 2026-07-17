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

## Verification

**Conclusion: Confirmed.**

The branch calls `virts.resize(imf.sum(0))`. `iMultiFab::sum` defaults
`local` to false, so every rank initially allocates the globally reduced
occupancy even though the subsequent MFIter loop writes and retains only
locally owned virtual particles.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This over-allocation occurs solely while `CreateVirtualParticles` performs
cell aggregation. Quokka has no call to that API and no code that consumes an
AMReX virtual-particle tile. Its multilevel gravity path deposits the real
particle containers to cell-centered `MultiFab`s directly, so it never
allocates `virts` from the aggregation occupancy `iMultiFab`.

Because the owning function is absent from Quokka's call graph, MPI size and
particle occupancy cannot expose this global/local count error in Quokka. A
new virtual-particle implementation would be required, making non-impact
definite.

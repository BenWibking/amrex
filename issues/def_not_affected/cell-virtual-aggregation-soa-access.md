# Cell virtual aggregation accesses SoA components through the AoS object

## Severity

High

## Affected code

The `particles.aggregation_type = Cell` branch of
`ParticleContainer_impl::CreateVirtualParticles` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

The deposition callbacks request `const ParticleType&`, which makes
`ParticleToMesh` pass the legacy AoS particle for a hybrid container. The code
then reads compile-time array components as
`p.rdata(NStructReal+i)` and `p.idata(NStructInt+i)`. Those components are not
members of `ParticleType`; they live in the tile's SoA arrays, so these reads
are out of bounds.

The reconstruction path has the same storage assumption for component zero:
it writes `p.rdata(0)` in the destination AoS. Pure-SoA and hybrid layouts with
the mass component in SoA are consequently unsupported but are neither
constrained nor rejected. Device out-of-bounds access can corrupt the
aggregated virtual particles or fail the kernel.

## Proposed patch

Implement the callbacks in terms of particle-tile data plus an index, or use a
super-particle representation that deliberately gathers both AoS and SoA
compile-time components. Reconstruct each result through storage-aware tile
accessors. Define and document where the aggregation weight lives, and reject
unsupported layouts with an always-on diagnostic.

Add `Cell` aggregation regressions for hybrid real/int array components and a
pure-SoA container.

## Verification

**Conclusion: Confirmed.**

The deposition callbacks accept `ParticleType const&` and read array fields as
`p.rdata(NStructReal+i)` and `p.idata(NStructInt+i)`. In a hybrid layout those
indices address beyond the AoS particle because array components live in the
tile's SoA. Reconstruction also unconditionally writes weight component zero
through the AoS proxy, which is not a layout-neutral operation.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The faulty accesses require both the cell-aggregation
`CreateVirtualParticles` branch and a hybrid AoS-plus-SoA particle layout.
Quokka uses neither: it never creates AMReX virtual particles, and every
Quokka particle field is stored in the compile-time AoS `Particle` rather
than `NArrayReal`/`NArrayInt` arrays.

With no call and no matching layout, the out-of-range AoS component indices
cannot be instantiated by Quokka. Two independent structural changes would be
needed to make the report relevant, so it definitely does not affect current
Quokka.

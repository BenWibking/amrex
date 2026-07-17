# Radius neighbor optimization misses interior AMR boundaries

## Severity

High

## Affected code

The position-based early exit in
`NeighborParticleContainer_impl::getNeighborTags` in
`Src/Particle/AMReX_NeighborParticlesI.H`.

## Explanation

When `search_radius > 0`, tag generation returns immediately if a particle is
farther than the radius from every face of its current source tile. That is a
valid same-level optimization only when all destination boundaries coincide
with source tile faces.

For a multi-level hierarchy, a fine-grid boundary can lie inside a coarse
tile. A coarse particle may be far from all coarse tile faces yet within the
requested physical radius of that fine region. The early return runs before
the code examines any target-level BoxArrays, so the required cross-level
neighbor tag is never generated.

The strict comparisons also skip a particle exactly one `search_radius` from
all source faces even if the interaction predicate treats the radius as
inclusive.

## Proposed patch

Restrict the tile-face early exit to a proven single-level case, or replace it
with a conservative test that accounts for boundaries of every candidate
destination level and periodic image. Use inclusive boundary semantics
consistent with the documented interaction radius.

Add a two-level test with a fine box wholly inside a coarse tile and a coarse
particle near the fine-box boundary but far from the coarse tile boundary.
Verify the radius fill creates the cross-level neighbor.

## Verification

**Conclusion: Confirmed.**

`getNeighborTags()` returns before iterating destination levels when a particle
is farther than `search_radius` from its source tile faces. In a multi-level
hierarchy, fine-grid boundaries can lie wholly inside a coarse tile, so those
faces do not conservatively bound the later cross-level intersection search.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The faulty early exit is confined to
`NeighborParticleContainer::getNeighborTags()`, the radius-based neighbor-tag
construction used by AMReX's neighbor-particle subsystem. Quokka does not
instantiate `NeighborParticleContainer`, request neighbor tags, or call the
radius-neighbor update path; its particle species use ordinary
`AmrParticleContainer` aliases.

Quokka can have multilevel particle hierarchies, but hierarchy depth alone is
not enough to reach this code. Without a neighbor container there is no
`getNeighborTags()` call in which the coarse-tile face test could suppress a
fine-level search, so the affected implementation is structurally outside
Quokka's current particle paths.

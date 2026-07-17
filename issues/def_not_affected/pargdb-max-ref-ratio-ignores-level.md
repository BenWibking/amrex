# ParGDB::MaxRefRatio ignores its level argument

## Severity

Medium

## Affected code

`ParGDB::MaxRefRatio` in `Src/Particle/AMReX_ParGDB.H`.

## Explanation

The interface takes a level and the `AmrParGDB` implementation returns the
maximum directional component of that level's refinement ratio. The standalone
`ParGDB` implementation ignores the argument and instead takes the maximum over
every refinement ratio in the hierarchy.

For a hierarchy with nonuniform ratios, asking about one transition therefore
returns another level's larger ratio. Code that uses this value for subcycling,
buffer widths, or timestep ratios can do too many substeps or construct the
wrong extent.

## Proposed patch

Validate `0 <= level < m_rr.size()` and return `m_rr[level].max()`, matching
`AmrMesh::MaxRefRatio` and `AmrParGDB` semantics.

Add a standalone multi-level `ParGDB` test with distinct anisotropic ratios and
verify each `MaxRefRatio(level)` independently.

## Verification

**Conclusion: Confirmed.**

`ParGDB::MaxRefRatio()` explicitly ignores its argument and scans every
transition. `AmrMesh::MaxRefRatio(lev)` and `AmrParGDB::MaxRefRatio(level)`
both return the maximum directional ratio for the requested transition, and
callers use the argument with that per-level meaning.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka's production particle containers use `AmrParGDB`, whose
`MaxRefRatio(level)` implementation is explicitly identified here as correct.
Quokka's own time-subcycling calls `AmrCore`/`AmrMesh::MaxRefRatio`, also the
correct implementation. The only standalone `ParGDB` objects in Quokka are
one-level temporary analysis containers, which have no refinement transition
and never call `MaxRefRatio`.

Thus every Quokka caller resolves to a different implementation, and the one
buggy implementation has no relevant call. Even nonuniform Quokka AMR ratios
are obtained from `AmrCore`, so current impact is definitely absent.

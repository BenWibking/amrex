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


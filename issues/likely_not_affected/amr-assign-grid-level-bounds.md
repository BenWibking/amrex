# AMR particle grid assignment can index invalid levels

## Severity

High

## Affected code

`AmrAssignGrid::operator()` in `Src/Particle/AMReX_ParticleLocator.H`.

## Explanation

The callable accepts runtime `lev_min` and `lev_max` values but does not verify
that they lie in `[0, m_size)`. Values other than the exact `-1` sentinel pass
through unchanged, and the function indexes `m_funcs[lev]` directly. A caller
can therefore read before or after the locator array.

The default path is also unsafe when the locator contains zero levels:
`m_size - 1` underflows as `size_t` before conversion to `int`, and the fallback
unconditionally accesses `m_funcs[lev_min]`. Vector-based locator construction
permits an empty hierarchy, so this state is reachable through the public API.

## Proposed patch

Return `(-1, -1, -1)` immediately when `m_size == 0`. After expanding the `-1`
sentinels, validate `0 <= lev_min <= lev_max < m_size` with a device-compatible
always-on check (or document and safely return failure for invalid ranges)
before indexing the assignor array.

Add host/GPU tests for an empty locator, both default sentinels, each valid
boundary level, negative values other than `-1`, reversed ranges, and a
`lev_max` equal to the level count.

## Verification

**Conclusion: Confirmed.**

`AmrAssignGrid::operator()` expands only the exact `-1` sentinels and then
indexes `m_funcs[lev]` without validating either bound or their ordering. With
zero levels, `m_size - 1` also underflows before conversion and the fallback
unconditionally indexes `m_funcs[lev_min]`.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka reaches `AmrAssignGrid` indirectly through ordinary particle
redistribution, but its checked-in calls supply hierarchy-derived bounds:
`Redistribute()` uses the full active hierarchy, post-regrid calls start at a
valid `lev`, and subcycled calls use `lev` through the container's current
`finestLevel()`. Quokka also constructs every production particle container
against an initialized `AmrCore`, so the locator has at least level zero. None
of those paths supplies a negative value other than AMReX's documented `-1`
sentinel, a reversed interval, or a level equal to the level count.

This is not classified as definitely unaffected because the faulty assignor
is on a central Quokka redistribution path and several level values are
runtime state that changes during regridding and restart refinement. A future
call-site error or an inconsistent transient hierarchy could expose it.
Current Quokka invariants avoid the trigger, making likely-not-affected the
appropriate present assessment.

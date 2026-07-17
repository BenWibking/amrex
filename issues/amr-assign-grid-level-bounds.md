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


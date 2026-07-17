# Empty NeighborList reports a negative particle count

## Severity

Low

## Affected code

`NeighborList::numParticles` in `Src/Particle/AMReX_NeighborList.H`.

## Explanation

The particle count is computed as `m_nbor_offsets.size() - 1`. Before the first
build, or after construction of an otherwise empty list whose offsets have not
been initialized, `size()` is the unsigned value zero. Subtracting one wraps to
the maximum `size_t`, which is then converted to `int`. Common platforms yield
`-1`, but the conversion is implementation-defined when the wrapped value is
not representable.

Callers reasonably expect an empty neighbor list to contain zero particles.
The negative result can corrupt loop bounds or size calculations outside the
current `print` implementation.

## Proposed patch

Return zero when `m_nbor_offsets.empty()`; otherwise return `size() - 1`, with
an explicit checked conversion if `int` remains the public return type. Add a
unit test for a default-constructed list and for a successfully built list with
zero source particles.

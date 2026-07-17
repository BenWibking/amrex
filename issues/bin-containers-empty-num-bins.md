# Default bin containers report negative bin counts

## Severity

Low

## Affected code

`DenseBins::numBins` in `Src/Particle/AMReX_DenseBins.H` and
`SparseBins::numBins` in `Src/Particle/AMReX_SparseBins.H`.

## Explanation

Both functions return `m_offsets.size() - 1`. Before the first successful
build, `m_offsets.size()` is the unsigned value zero, so the subtraction
underflows before conversion to `Long`. On common platforms the observable
result is `-1`, but the conversion is implementation-defined when the unsigned
value does not fit.

An empty container should report zero bins. The current result can break loops,
size calculations, or validation performed before or after an empty build.

## Proposed patch

Return zero when the offset vector is empty, for example:

```cpp
return m_offsets.empty() ? 0 : static_cast<Long>(m_offsets.size() - 1);
```

Add tests for a default-constructed container, an empty build, and a nonempty
build for both dense and sparse implementations.


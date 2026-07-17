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

## Verification

**Conclusion: Confirmed.**

Both `DenseBins::numBins()` and `SparseBins::numBins()` return
`m_offsets.size() - 1`. A default-constructed container has an empty offsets
vector, so the unsigned subtraction wraps instead of reporting zero bins.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka never calls `DenseBins::numBins()` or `SparseBins::numBins()`. Its
particle code does not instantiate `SparseBins` at all, and the AMReX dense-bin
locator paths it uses build and consume iterators directly rather than querying
the default object's bin count. A repository-wide call-site search finds the
two declarations but no Quokka or relevant AMReX particle call that observes
the wrapped default value.

Since the defect changes only the return value of these unused query methods,
it cannot influence Quokka's particle location, deposition, or redistribution.
A new explicit bin-introspection feature would be needed to make it relevant,
so the issue is definitely outside current Quokka behavior.

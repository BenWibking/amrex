# RedefineDummyMF indexes an empty vector

## Severity

High

## Affected code

`ParticleContainerBase::RedefineDummyMF` in
`Src/Particle/AMReX_ParticleContainerBase.cpp`.

## Explanation

The growth check is written as:

```cpp
if (lev > m_dummy_mf.size()-1) { m_dummy_mf.resize(lev+1); }
```

`size()` is unsigned. When `m_dummy_mf` is empty, subtracting one wraps to the
largest `size_t`; the nonnegative `lev` is converted to that unsigned type, so
the condition is false. The next expression indexes `m_dummy_mf[lev]` even
though the vector still has size zero.

Normal `resizeData` calls pre-size the vector, but `RedefineDummyMF` is public
and is also reached by the public per-level BoxArray and DistributionMapping
setters. A base constructed around an external `ParGDBBase` can reach this path
before any dummy data were allocated.

## Proposed patch

Require `lev >= 0` and compare it directly with `std::ssize(m_dummy_mf)`:

```cpp
if (lev >= std::ssize(m_dummy_mf)) {
    m_dummy_mf.resize(lev + 1);
}
```

Add a regression that constructs the container around an external `ParGDB`,
leaves `m_dummy_mf` empty, and calls both `RedefineDummyMF(0)` and a per-level
grid setter.

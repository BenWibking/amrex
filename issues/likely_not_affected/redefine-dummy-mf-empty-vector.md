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

## Verification

**Conclusion: Confirmed.**

`RedefineDummyMF()` tests `lev > m_dummy_mf.size()-1`. With an empty vector,
the unsigned subtraction wraps and the resize branch is skipped for
nonnegative `lev`; the next expression indexes the still-empty vector. The
method is public and is reached by public per-level grid setters.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka's production particle containers are attached to an `AmrCore` with a
defined level-zero grid before per-level setters or redistribution run, so
their dummy-`MultiFab` vector is sized for at least one level. The temporary
analysis containers in `particle_IO.hpp` likewise call `Define` with a
single-box level-zero hierarchy before creating a tile. Restart refinement
updates existing level entries rather than calling a per-level setter on a
never-defined empty container.

The vulnerable public setters are used by Quokka's restart-refinement helper,
and their safety depends on the current initialization order. A refactor that
sets level metadata before `Define`, or an early failure that leaves the vector
empty, could expose the underflow. Present ordering makes impact unlikely but
does not make the path impossible.

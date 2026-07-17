# ParticleTile range insertion dereferences the end iterator

## Severity

High

## Affected code

The `amrex::Vector` iterator overloads of `ParticleTile::push_back_real` and
`ParticleTile::push_back_int` in `Src/Particle/AMReX_ParticleTile.H`.

## Explanation

Both overloads convert the iterator pair to pointers with `&(*beg)` and
`&(*end)`. Dereferencing `end` is undefined because it is the one-past-end
iterator. The empty-range case is worse: `beg == end`, so both expressions
dereference the same invalid iterator.

The overloads taking a complete `amrex::Vector` always route through this
code, so ordinary nonempty vectors trigger undefined behavior even though the
resulting pointer value may appear to work with common standard-library
implementations. Empty vectors can fail immediately under iterator debugging
or sanitizers.

## Proposed patch

Pass the iterators directly to the destination `PODVector::insert` overload
instead of manufacturing pointers:

```cpp
auto& data = m_soa_tile.GetRealData(comp);
data.insert(data.end(), beg, end);
```

Apply the equivalent change to integer components. Add focused tests for
nonempty and empty iterator ranges and for the full-vector convenience
overloads, preferably under a debug standard library or UBSan build.

## Verification

**Conclusion: Confirmed.**

The iterator-range overload computes raw endpoints as `&(*beg)` and
`&(*end)`. Dereferencing the past-the-end iterator is undefined for every
nonempty range, and both dereferences are invalid for an empty range.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The affected overloads append `amrex::Vector` ranges to SoA real or integer
component arrays. Quokka has no SoA array components and contains no
`push_back_real(comp, begin, end)` or `push_back_int(comp, begin, end)` call.
Its particle insertion either pushes a complete AoS particle or resizes a tile
and assigns AoS fields in a kernel.

The malformed iterator conversion is therefore never instantiated or
executed for Quokka. A new SoA layout and range-based insertion path would be
needed, so non-impact is definite.

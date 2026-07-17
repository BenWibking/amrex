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

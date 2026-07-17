# Default ParGDB has an indeterminate level count

## Severity

High

## Affected code

The default constructor and `m_nlevels` member in
`Src/Particle/AMReX_ParGDB.H`.

## Explanation

`ParGDB()` is explicitly defaulted, while `m_nlevels` has no default member
initializer. A default-constructed object therefore contains an indeterminate
integer. Calling `finestLevel`, `maxLevel`, or `LevelDefined` before assigning a
fully populated `ParGDB` reads that indeterminate value, which is undefined
behavior and can make an empty database appear to have arbitrary levels.

Default construction is part of the public API and is also used internally as
an intermediate ownership state, so the object should have a deterministic
empty invariant.

## Proposed patch

Initialize the member in its declaration:

```cpp
int m_nlevels{0};
```

Add a small test that default-constructs `ParGDB` and verifies
`finestLevel() == -1`, `maxLevel() == -1`, and that no integer level is reported
defined.

## Verification

**Conclusion: Confirmed.**

`ParGDB()` is defaulted and `m_nlevels` has no default member initializer.
`finestLevel()`, `maxLevel()`, and `LevelDefined()` read that member directly,
so these public queries on the public default state read an indeterminate
integer.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Production Quokka particle containers are constructed with the simulation's
initialized `AmrCore`, so they use `AmrParGDB` rather than querying a default
standalone `ParGDB`. Quokka does default-construct temporary analysis
containers in `particle_IO.hpp`, but it immediately calls `Define` with a
single valid geometry, `BoxArray`, and `DistributionMapping` before asking for
levels, defining tiles, or redistributing particles. The indeterminate default
level count is therefore not read along the current analysis path.

Because a default standalone container really does exist transiently in
Quokka, the issue cannot be ruled out as structurally impossible. A new query,
early error path, or refactor between construction and `Define` could expose
it. The present ordering avoids all such queries, supporting a likely-not-
affected classification.

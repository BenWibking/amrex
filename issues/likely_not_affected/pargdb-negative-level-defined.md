# ParGDB reports negative levels as defined

## Severity

Low

## Affected code

`ParGDB::LevelDefined` in `Src/Particle/AMReX_ParGDB.H`.

## Explanation

The implementation checks only `level < m_nlevels`. For every nonempty
database, all negative integers satisfy that expression, so
`LevelDefined(-1)` returns true even though using `-1` with any of the level
accessors indexes before their vectors.

Callers commonly use `LevelDefined` as the guard that makes subsequent level
access safe. Returning true for a negative level defeats that guard and can
turn an exhausted level-search loop into an out-of-bounds access.

## Proposed patch

Require both bounds:

```cpp
return level >= 0 && level < m_nlevels;
```

Add boundary tests for `-1`, `0`, `m_nlevels - 1`, and `m_nlevels` on empty and
populated databases.

## Verification

**Conclusion: Confirmed.**

`ParGDB::LevelDefined()` implements only `level < m_nlevels`. For every
nonempty database, `LevelDefined(-1)` is therefore true even though all level
accessors would index their vectors with that negative value.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka's production containers use the `AmrParGDB` supplied by `AmrCore`, not
the affected standalone `ParGDB` implementation. Its temporary standalone
analysis containers define a one-level database before redistribution and
operate only on level zero. Checked-in loops over particle data start at zero
and stop at `finestLevel()`, and no Quokka call asks `LevelDefined` about a
negative value.

The conclusion is not definite because standalone `ParGDB` is instantiated by
Quokka's analysis helpers and AMReX itself can use `LevelDefined` while
searching an evolving hierarchy. If an empty temporary or exhausted search
were introduced, `-1` could reach the faulty predicate. Current construction
and loop invariants keep that from happening, so impact is unlikely rather
than impossible.

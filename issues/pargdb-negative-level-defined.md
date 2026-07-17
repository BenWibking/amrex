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


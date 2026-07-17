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


# Deposition permutation produces invalid bins for negative refined indices

## Severity

High

## Affected code

The particle-tile overload of `PermutationForDeposition` in
`Src/Particle/AMReX_ParticleUtil.H`.

## Explanation

For stagger types encoded with `idx_type == 2`, the routine refines the
geometry by two and decomposes each refined integer index into a coarse index
and a subcell remainder:

```cpp
IntVect iv_coarse = iv / refine_vect;
IntVect iv_remainder = iv - iv_coarse * refine_vect;
```

`IntVect::operator/` uses ordinary C++ integer division, which truncates toward
zero. For a valid negative index such as `iv == -1` with ratio two, it produces
`iv_coarse == 0` and `iv_remainder == -1`. The remainder is then used to select
one of the refined bin planes, yielding a negative bin offset. The linked-list
builder indexes `llist_start + f(i)` with that value and writes outside the
allocated bin array.

AMReX index spaces can legitimately have negative small ends, so this is not
limited to particles outside the domain.

## Proposed patch

Use AMReX floor-coarsening semantics component-wise, for example
`coarsen(iv, refine_vect)`, then compute `iv_remainder = iv - iv_coarse *
refine_vect`. The remainder will be in `[0, ratio)` in every direction.

Add deposition-permutation tests for boxes spanning negative indices and for
every supported `idx_type`, checking that the permutation is complete and all
computed bins lie in the allocated range.

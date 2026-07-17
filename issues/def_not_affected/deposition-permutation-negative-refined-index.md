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

## Verification

**Conclusion: Confirmed.**

The permutation code uses `IntVect::operator/`, which performs C++ signed
integer division, to derive a coarse index. For example, `-1 / 2` truncates to
zero, leaving remainder `-1`; that negative remainder is then used in the bin
offset. AMReX's `coarsen` path has the required floor semantics instead.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This code runs only through `PermutationForDeposition`, normally from
`SortParticlesForDeposition` with an `idx_type` containing the special value
two. Quokka never calls either function. Its `ParticleToMesh` uses direct
per-tile loops and Quokka interpolation functors; it does not create or consume
the deposition permutation before depositing mass, radiation, or counts.

Negative AMR indices in a Quokka domain therefore do not encounter this
integer decomposition. The relevant sorting API is absent from Quokka's call
graph, which makes non-impact definite.

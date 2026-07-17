# Mask-only filterParticles overload cannot be called

## Severity

Medium

## Affected code

The three-argument mask overload of `filterParticles` in
`Src/Particle/AMReX_ParticleTransformation.H`.

## Explanation

The overload is declared with template parameters `Index` and `N`, but `N`
does not appear in any function parameter or return type:

```cpp
template <typename DstTile, typename SrcTile,
          std::integral Index, typename N>
Index filterParticles(DstTile&, const SrcTile&, const Index* mask);
```

Normal calls provide no way to deduce `N`, so overload resolution fails even
though the function body simply forwards `src.numParticles()` to the ranged
implementation. Users must either specify an otherwise meaningless explicit
template argument or avoid the documented convenience API.

## Proposed patch

Remove the unused `N` template parameter from this overload. Add a compile/run
test that calls `filterParticles(dst, src, mask)` without explicit template
arguments on CPU and GPU builds.

## Verification

**Conclusion: Confirmed.**

The three-argument mask overload declares an unused template parameter `N`.
None of its function arguments contains `N`, so normal template argument
deduction cannot select this documented convenience overload.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka never calls the three-argument mask form of `filterParticles`. Its own
particle creation/destruction uses GPU predicates and ID invalidation followed
by redistribution. The AMReX paths Quokka reaches either do not filter or use
the separately valid predicate/count overloads; async output, which has its
own `KeepValidFilter` call, is rejected by Quokka and is not this convenience
signature.

Because no Quokka expression requires template deduction for the malformed
overload, it cannot cause a Quokka compile failure. A new explicit mask-based
transformation call would be required, making current non-impact definite.

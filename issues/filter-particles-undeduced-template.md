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


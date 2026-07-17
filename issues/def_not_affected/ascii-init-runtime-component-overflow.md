# ASCII initialization indexes compile-time storage for runtime components

## Severity

High

## Affected code

`ParticleContainer_impl::InitFromAsciiFile` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

The accepted upper bound for `extradata` includes `NumRealComps()`, which is
the sum of compile-time and runtime SoA components. The host staging structure,
however, is a fixed `std::array<..., NArrayReal>`, and indexes it with
`n - NStructReal` for every requested non-struct component.

If the input includes any runtime real component, that index reaches or
exceeds `NArrayReal`. The code then writes beyond the staging array or silently
fails to copy the runtime values, while the destination tile is expected to
have the container's full runtime layout.

## Proposed patch

Use a dynamically sized vector of host component arrays with
`NumRealComps()` entries, define destination tiles with the runtime layout, and
copy both compile-time and runtime values. If runtime ASCII import is not
supported, cap and validate `extradata` at `NStructReal + NArrayReal` with an
always-on diagnostic.

Add a runtime-real ASCII round trip and an out-of-range rejection test.

## Verification

**Conclusion: Confirmed.**

The entry assertion permits `extradata` through `NStructReal +
NumRealComps()`, including runtime arrays, but redistribution stages fields in
a fixed `std::array<..., NArrayReal>`. The conditional paths either suppress
the copy or would index that array by the runtime component offset; there is no
runtime-array staging or supported rejection.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The overflow requires an ASCII import to request at least one runtime SoA real
component beyond `NArrayReal`. Every Quokka particle alias is an
`AmrParticleContainer<NStructReal,NStructInt>` with all physics fields in the
compile-time AoS `Particle` type. Quokka never calls `AddRealComp` or
`ResizeRuntimeRealComp`, so `NumRuntimeRealComps()` is zero for every container
that reaches `InitFromAsciiFile`.

Quokka's `extradata` values therefore range only over AoS struct fields and
cannot index the fixed SoA staging array at a runtime-component offset. The
necessary layout does not exist in Quokka, making this a definite rather than
probabilistic exclusion.

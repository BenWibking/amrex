# StructOfArrays::realarray uses the field precision for local storage

## Severity

Medium

## Affected code

`StructOfArrays::realarray` in `Src/Particle/AMReX_StructOfArrays.H`.

## Explanation

The compile-time real component vectors store `ParticleReal`, and the function
returns `GpuArray<ParticleReal*, NReal>`. Its local array is instead declared as
`GpuArray<Real*, NReal>`.

AMReX supports configuring field precision (`Real`) independently from
particle precision (`ParticleReal`). When those aliases differ, pointers from
the particle component vectors cannot be stored in the local `Real*` array (or
the local array cannot be returned as the declared particle-pointer array), so
instantiating `realarray()` fails to compile in a supported mixed-precision
configuration.

## Proposed patch

Declare the local variable with the function's actual element type:

```cpp
GpuArray<ParticleReal*, NReal> arr;
```

Add a compile test that enables differing field and particle precisions and
instantiates `StructOfArrays<...>::realarray()` with at least one compile-time
real component.

## Verification

**Conclusion: Confirmed.**

`StructOfArrays::realarray()` promises `GpuArray<ParticleReal*, NReal>` but
constructs a local `GpuArray<Real*, NReal>`. AMReX supports distinct field and
particle precision, and the pointer-array types are not convertible in that
configuration, so the accessor fails to compile.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The type mismatch is instantiated by `StructOfArrays::realarray()` for a
container with compile-time SoA real components, especially when AMReX field
and particle precision differ. Quokka's particle aliases have zero
compile-time SoA real arrays: their user real fields are part of the AoS
particle struct, and Quokka has no call to `realarray()`.

As a result, compiling Quokka does not instantiate the incompatible
`GpuArray<Real*>`-to-`GpuArray<ParticleReal*>` return path. This remains true
independently of Quokka's selected precision because the required accessor
and SoA component layout are both absent.

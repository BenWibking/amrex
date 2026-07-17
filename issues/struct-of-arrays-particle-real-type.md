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


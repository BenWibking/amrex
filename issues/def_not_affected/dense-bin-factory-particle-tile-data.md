# Dense-bin iterator factory cannot be instantiated for ParticleTileData

## Severity

Medium

## Affected code

`DenseBinIteratorFactory` in `Src/Particle/AMReX_DenseBins.H`.

## Explanation

`DenseBins` explicitly supports both raw item pointers and
`ParticleTileData`: its `const_pointer_type` becomes `T` for particle-tile data,
and its build functions accept `const T&`. The iterator factory stores the same
conditional `const_pointer_type`, but its constructor unconditionally accepts
`const T* items`.

For `DenseBins<ParticleTileData>`, `getBinIteratorFactory()` passes the stored
`ParticleTileData` value to a constructor requiring a pointer to
`ParticleTileData`. Instantiating that otherwise-supported API therefore fails
to compile. `SparseBinIteratorFactory` already uses the correct conditional
input type and shows the intended pattern.

## Proposed patch

Add the same `const_pointer_input_type` alias used by `DenseBins` and
`SparseBinIteratorFactory`, and change the dense factory constructor's final
parameter to that type. Retain value storage for particle-tile data and pointer
storage for ordinary item arrays.

Add a compile-and-run test that builds `DenseBins<ParticleTileData>`, obtains
its iterator factory, and iterates one bin.

## Verification

**Conclusion: Confirmed.**

`DenseBins` conditionally stores `ParticleTileData` by value, but
`DenseBinIteratorFactory` accepts its final constructor argument as `T const*`.
`getBinIteratorFactory()` forwards `m_items` directly, so this supported
instantiation cannot compile. `SparseBins` already uses a conditional input
type for the corresponding factory.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The compile failure is specific to instantiating
`DenseBinIteratorFactory<ParticleTileData>`. Quokka does not request that
factory. AMReX particle location for Quokka instantiates
`DenseBinIteratorFactory<Box>`, while Quokka's particle-mesh kernels iterate
tiles directly and do not ask `DenseBins` to return a factory over
`ParticleTileData`.

Template errors in an uninstantiated specialization do not enter Quokka's
build. A new bin-based particle-data query would be needed to instantiate it,
so current Quokka is definitely unaffected.

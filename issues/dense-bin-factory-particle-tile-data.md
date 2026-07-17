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


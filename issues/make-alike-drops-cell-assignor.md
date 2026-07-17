# make_alike drops the container's CellAssignor

## Severity

Medium

## Affected code

`ParticleContainer_impl::ContainerLike` and `make_alike` in
`Src/Particle/AMReX_ParticleContainer.H`.

## Explanation

`ParticleContainer_impl` has a `CellAssignor` template parameter that controls
how positions map to mesh cells. The `ContainerLike` alias preserves particle
type and compile-time components but instantiates the new container without
that final template argument:

```cpp
ParticleContainer_impl<ParticleType, NArrayReal, NArrayInt, NewAllocator>
```

It therefore selects `DefaultAssignor`, even when the source container uses a
custom policy. `make_alike()` promises the same compile-time and runtime
attributes with only an optional allocator change, but its result can compute
different indices, destinations, and redistribution behavior.

The return type also makes this silent at compile time unless the caller
explicitly inspects it.

## Proposed patch

Include `CellAssignor` in the alias:

```cpp
ParticleContainer_impl<ParticleType, NArrayReal, NArrayInt,
                       NewAllocator, CellAssignor>
```

Add a compile-time type assertion and a runtime test using a visibly custom
assignor, checking that `Index` and redistribution agree between a container
and its `make_alike()` result.

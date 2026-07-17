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

## Verification

**Conclusion: Confirmed.**

`ContainerLike` supplies only four template arguments to
`ParticleContainer_impl`, omitting the source `CellAssignor` and selecting the
default. `make_alike()` returns that alias while promising the same compile-
time and runtime attributes except for allocator choice, so a custom mapping
policy is silently changed.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka never calls `make_alike` or constructs the affected `ContainerLike`
alias. Its temporary analysis containers are default-constructed from the
original concrete `ContainerType` and then explicitly defined. In addition,
all Quokka particle containers use AMReX's default `CellAssignor`; there is no
custom assignor policy for `make_alike` to lose.

The bug thus has neither a call site nor distinct source/destination assignor
semantics in Quokka. Both would need to be introduced, so current non-impact
is definite.

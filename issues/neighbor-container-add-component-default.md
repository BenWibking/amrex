# Neighbor component-add defaults cannot be called

## Severity

Medium

## Affected code

`NeighborParticleContainer_impl::AddRealComp` and `AddIntComp` in
`Src/Particle/AMReX_NeighborParticles.H`.

## Explanation

Both functions are templates whose type parameter must be deduced from the
`communicate` argument:

```cpp
template <std::same_as<bool> T>
void AddRealComp (T communicate=true);
```

Default function arguments do not participate in template argument deduction.
Consequently `container.AddRealComp()` and `container.AddIntComp()` cannot
deduce `T` and fail to compile, despite explicitly advertising a default.

These derived declarations also hide the base-class overload set, so the
working no-argument base overload is not found through a neighbor container.
The result is inconsistent with ordinary particle containers and forces users
to pass a redundant `true` value.

## Proposed patch

Make these ordinary `bool` overloads, or give the template parameter a default
such as `T = bool`. If named component addition should remain available, expose
the base overloads with `using ParticleContainerType::AddRealComp` and
`AddIntComp` or add neighbor-aware named overloads that also extend the ghost
component masks and recalculate communication size.

Add compile-and-run tests for no-argument, explicit-boolean, and named runtime
component addition on a neighbor container.

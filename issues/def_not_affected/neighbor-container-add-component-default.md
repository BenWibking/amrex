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

## Verification

**Conclusion: Confirmed.**

The derived no-name overloads are templates whose only deducible occurrence
of `T` is the `communicate` parameter. Omitting that parameter cannot deduce
`T`, and the declarations hide the base overloads of the same names. Thus the
advertised default call is ill-formed.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The malformed overloads are members of `NeighborParticleContainer_impl`.
Quokka neither instantiates that type nor calls `AddRealComp`/`AddIntComp` on
any particle container. All Quokka components are fixed by compile-time
particle aliases, so no source expression asks the compiler to deduce the
hidden neighbor overload's template argument.

An uninstantiated ill-formed convenience call cannot break Quokka's build.
Runtime component support on a new neighbor container would have to be added
first, so current impact is definitely absent.

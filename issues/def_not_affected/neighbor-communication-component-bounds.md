# Neighbor communication component setters lack bounds checks

## Severity

Medium

## Affected code

`NeighborParticleContainer_impl::setRealCommComp` and `setIntCommComp` in
`Src/Particle/AMReX_NeighborParticlesI.H`.

## Explanation

The public setters index `ghost_real_comp[i]` and `ghost_int_comp[i]` directly
without checking either the lower or upper bound. A negative index writes
before the vector, and an index equal to or greater than the corresponding
communication-component count writes beyond it. The subsequent
`calcCommSize()` can then compute a record size from already-corrupted state.

The valid index space is not self-evident for AoS particles because the real
mask includes position and struct components, while the integer mask includes
ID/CPU and struct components.

## Proposed patch

Use always-on checks requiring `0 <= i < numRealCommComps()` or
`numIntCommComps()` before modifying the masks. Document the full component
layout and provide named helpers if users are not expected to address packed
position/ID slots directly.

Add tests for the first and last valid components and for negative and
one-past-end indices in both masks.

## Verification

**Conclusion: Confirmed.**

`setRealCommComp()` and `setIntCommComp()` directly subscript their mask vectors
with a signed public argument and perform no lower- or upper-bound check before
recomputing the communication record size.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

`setRealCommComp` and `setIntCommComp` are controls on
`NeighborParticleContainer`. Quokka has no neighbor container and no call to
either setter; its ordinary particle redistribution communicates the complete
compile-time AoS record selected by the base container. Quokka's component
enums are not forwarded into neighbor communication masks.

Since the owning class is uninstantiated, even an invalid Quokka component
index cannot reach these vector subscripts. The subsystem exclusion makes the
classification definite.

# Neighbor communication controls leak between containers

## Severity

High

## Affected code

The static `use_mask` and `enable_inverse` fields and their instance methods in
`Src/Particle/AMReX_NeighborParticles.H` and
`Src/Particle/AMReX_NeighborParticlesI.H`.

## Explanation

Both controls are static members of a container template specialization, so
all containers with the same particle type share them. Their behavior is
nevertheless managed as if it were per object:

- `BuildMasks` assigns `use_mask` based on that container's number of levels.
- `setEnableInverse` assigns `enable_inverse` and calls `calcCommSize()` only
  for the container on which it was invoked.

If two matching containers coexist, building masks for a single-level
container can switch a multi-level container into the wrong lookup path.
Likewise, enabling inverse communication on one container changes
`enableInverse()` for the other without updating the other container's
`cdata_size`. Its pack/unpack code can then disagree about record layout and
overrun or misinterpret communication buffers.

## Proposed patch

Make `use_mask` and `enable_inverse` non-static data members. Keep mask-mode
selection and communication-size recalculation local to the owning container.
If a global configuration is desired, separate it from the derived per-object
state and force every affected container to recompute its layout.

Add tests with two same-specialization containers: use different hierarchy
depths and inverse settings, interleave mask builds/fills, and verify each
container retains its own mode and communication record size.

## Verification

**Conclusion: Confirmed.**

`use_mask` and `enable_inverse` are defined as static members per template
specialization. Instance methods mutate them, while `setEnableInverse()`
recalculates `cdata_size` only on the receiving object. A second same-type
container therefore observes the changed packing mode with a stale per-object
record size.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The leaked `use_mask` and `enable_inverse` state exists only for same-type
`NeighborParticleContainer` instances. Quokka owns zero such instances, does
not call the instance control methods, and performs no inverse neighbor
communication. Its multiple physics-particle containers are base
`AmrParticleContainer` specializations and do not share these static fields.

There is therefore no first Quokka container that can mutate the state and no
second one that can observe a stale record size. The affected class would need
to be integrated before the issue could apply, making non-impact definite.

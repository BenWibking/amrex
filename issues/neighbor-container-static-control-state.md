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

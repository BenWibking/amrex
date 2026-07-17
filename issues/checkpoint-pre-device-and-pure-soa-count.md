# CheckpointPre reads device AoS storage and counts pure-SoA containers as empty

## Severity

High

## Affected code

`ParticleContainer_impl::CheckpointPre` in
`Src/Particle/AMReX_ParticleIO.H`.

## Explanation

The pre-pass counts valid particles by indexing each tile's
`GetArrayOfStructs()` directly in a host loop. With a GPU allocator, that AoS
resides in device memory and cannot be dereferenced by the host. With a
pure-SoA particle type, the AoS contains no particles at all, even though the
tile's ID/CPU and component arrays do.

The first case can fault or read invalid memory. The second records
`nparticlesPrePost == 0`, which the synchronous checkpoint writer then places
in the global header even while nonzero per-grid particle records are written.

## Proposed patch

Use the existing tile-data reduction path, such as
`TotalNumberOfParticles(true, true)`, to count valid particles independently of
layout and memory space, followed by the required communicator reduction. Do
not inspect AoS storage as a proxy for particle existence.

Add pre/post checkpoint round trips for GPU AoS and pure-SoA containers and
verify the global header count against the grid records.

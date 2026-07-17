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

## Verification

**Conclusion: Confirmed.**

`CheckpointPre` in `Src/Particle/AMReX_ParticleIO.H` counts particles by
iterating `GetArrayOfStructs()` in a host loop and dereferencing each AoS
element. That is invalid for device-resident AoS data, and a pure-SoA tile has
no AoS elements even when it contains particles, so the cached header count is
wrong for both supported configurations.

## Quokka impact classification

**Classification: Likely affects Quokka.**

AMReX enables this pre-pass when `particles.use_prepost = 1`; the option is
parsed by every Quokka particle-container specialization because Quokka does
not intercept or forbid it. Quokka then writes tracers and registered physics
particles through the normal AMReX `Checkpoint` API. Quokka's particle aliases
are AoS `AmrParticleContainer`s, and its supported CUDA and HIP builds place
their AoS tiles in device memory. Consequently, a Quokka GPU run that enables
pre/post particle I/O and writes a checkpoint reaches the invalid host
dereference described by this issue. The pure-SoA half of the issue is not
relevant to Quokka, but the GPU-AoS half is.

The classification is conditional rather than definite because
`particles.use_prepost` defaults to false and no checked-in Quokka input deck
currently enables it. Under those current defaults `CheckpointPre` returns
before touching particle storage. Nevertheless, pre/post I/O is a documented
AMReX option that passes through Quokka, and particle checkpointing on GPUs is
a supported Quokka workflow, so the trigger is credible enough for likely
affected rather than likely not affected.

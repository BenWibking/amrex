# Restart silently appends to an already populated container

## Severity

Medium

## Affected code

`ParticleContainer_impl::Restart` in
`Src/Particle/AMReX_ParticleIO.H`.

## Explanation

`Restart` resizes the level vectors but never clears existing particle tiles.
`ReadParticles` explicitly appends every loaded batch after each destination
tile's current `size()`. Calling the public restart API on a nonempty container
therefore retains the old particles and adds all checkpoint particles, usually
creating duplicates after redistribution.

The API documentation does not state an empty-container precondition, and no
assertion or diagnostic detects the additive behavior.

## Proposed patch

Make restart replacement semantics explicit by clearing existing particles
before reading, after the header has been validated but before any batch is
appended. If additive import is intentional, expose it as a separately named
operation and reject nonempty state in `Restart`.

Add a regression that populates a container, restarts it from a checkpoint with
a distinct count/ID set, and verifies that only checkpoint particles remain.

## Verification

**Conclusion: Confirmed.**

`Restart` validates the checkpoint and resizes level metadata but never clears
`m_particles`. The read path obtains each existing destination tile, records
its `old_size`, grows it by the checkpoint count, and copies new data after
that offset. Calling `Restart` on a nonempty container therefore appends the
checkpoint particles rather than replacing the current state.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka's physics-particle restart helper asserts that each owning
`unique_ptr` is null, constructs a new container, registers it, and only then
calls AMReX `Restart`. The normal and restart-refinement branches therefore
load into an empty `m_particles` map. Restart refinement temporarily changes
the new container's geometry but still calls `Restart` exactly once before
redistribution. Existing checkpoint particles are not already present to be
duplicated.

The classification is not definite because Quokka calls the affected public
method directly and the empty-container precondition is enforced by its
current helper rather than AMReX. A future in-place reload, diagnostic restart,
or removed null assertion could make the bug immediately relevant. Current
ownership semantics avoid it, so likely not affected is appropriate.

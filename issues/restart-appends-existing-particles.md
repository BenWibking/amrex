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

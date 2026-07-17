# Particle I/O collectives ignore the active ParallelContext

## Severity

High

## Affected code

Checkpoint, restart, ASCII output, and binary output paths in
`Src/Particle/AMReX_ParticleIO.H` and
`Src/Particle/AMReX_WriteBinaryParticleData.H`.

## Explanation

Particle-container counting and redistribution generally use
`ParallelContext::CommunicatorSub()` and sub-context ranks. The I/O paths use
world-oriented `ParallelDescriptor` ranks, barriers, reductions, broadcasts,
and I/O-processor tests throughout.

If a particle container is operated by an active sub-context and only those
ranks call its I/O routine, world barriers/reductions can deadlock. If all world
ranks call with independent sub-context data, counts, file ownership, and rank
maps can be mixed across otherwise separate contexts.

## Proposed patch

Use the active sub-context communicator and rank/size/I/O-root consistently for
all particle I/O collectives and output-file grouping. Audit async output's
rank mapping at the same time, since it currently chooses a world rank as its
I/O processor. If particle I/O is intentionally world-only, enforce and
document that precondition before the first collective.

Add a split-communicator regression in which independent sub-contexts perform
particle output and restart without entering world collectives.

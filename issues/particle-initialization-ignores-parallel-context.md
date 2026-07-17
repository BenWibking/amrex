# Particle initialization ignores the active ParallelContext

## Severity

High

## Affected code

Distributed initialization routines in
`Src/Particle/AMReX_ParticleInit.H` and the reader-limit helpers they use.

## Explanation

The routines select readers, assign CPU fields, distribute random work, and
perform broadcasts/reductions with world-oriented `ParallelDescriptor` ranks
and sizes. Particle containers otherwise support active sub-contexts and use
sub-communicators for redistribution and global counts.

When only a sub-context calls an initializer, world collectives can deadlock.
Even if every world rank participates, world reader IDs and work partitioning
can conflict with the container's sub-context distribution maps and CPU/rank
semantics.

## Proposed patch

Use the active `ParallelContext` rank, size, root, and communicator consistently
through reader selection, broadcasts, reductions, random seeding, and CPU
assignment. Make `MaxReaders` context-aware rather than caching a world-sized
value on first use.

Add split-communicator initialization tests for ASCII, binary, and random
paths.

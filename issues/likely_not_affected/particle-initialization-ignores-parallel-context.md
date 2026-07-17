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

## Verification

**Conclusion: Confirmed.**

The distributed initializer implementations consistently take ranks, sizes,
roots, CPU values, broadcasts, and reductions from `ParallelDescriptor`.
Particle redistribution and ownership use `ParallelContext` sub-ranks and the
active sub-communicator, so invoking these routines within a pushed context
mixes incompatible rank spaces and collectives.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka performs tracer and physics-particle initialization during global
simulation startup. There is no `ParallelContext::push`/`pop` call in Quokka's
source around `InitFromAsciiFile`, `InitRandom`, or `InitOnePerCell`; at that
point the active sub-communicator is the world communicator, so the
`ParallelDescriptor` ranks and collectives used by AMReX agree with particle
ownership. Bottom-solver communicator changes happen later and do not enclose
particle initialization.

Quokka increasingly uses `ParallelContext::CommunicatorSub()` in its particle
reductions, so sub-context-aware execution is a relevant design direction and
an out-of-tree driver could push a context before constructing a simulation.
Because current startup does not, the issue is unlikely to affect checked-in
workflows but cannot be excluded for every embedding.

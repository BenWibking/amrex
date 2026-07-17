# Tracer timestamping mixes world ranks with sub-communicator sizes

## Severity

High

## Affected code

`TracerParticleContainer::Timestamp` in
`Src/Particle/AMReX_TracerParticles.cpp`.

## Explanation

The output scheduling code sets `NProcs` from
`ParallelContext::NProcsSub()` but sets `MyProc` from
`ParallelDescriptor::MyProc()`, which is the world rank. It then derives
`mySet`, output filenames, send/receive peers, and tags from that inconsistent
pair. The point-to-point calls also use the default world communicator.

For a parallel-context frame whose members do not have world ranks
`0..NProcsSub()-1`, `mySet` can be outside the loop's set range. Such a rank can
skip all output work and enter a receive from an unrelated world rank. This can
deadlock and can also make different sub-communicators collide in the same
timestamp files.

## Proposed patch

Use `ParallelContext::MyProcSub()` consistently for scheduling and filenames,
and pass `ParallelContext::CommunicatorSub()` explicitly to the point-to-point
operations. Replace the final world reduction with the existing
sub-communicator reduction pattern used by `AdvectWithUmac` and
`AdvectWithUcc`.

Add an MPI regression that pushes a nontrivial split communicator, invokes
`Timestamp` within each frame, and verifies completion plus disjoint, complete
records for every sub-communicator.

## Verification

**Conclusion: Confirmed.**

`TracerParticleContainer::Timestamp` combines
`ParallelDescriptor::MyProc()` with `ParallelContext::NProcsSub()` when
constructing its writer schedule, then performs `ParallelDescriptor::Send`,
`Recv`, and a world reduction. With a nontrivial active sub-context, the rank
and size belong to different communicators, so peers and collectives can be
incorrect or deadlock.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The communicator mismatch is wholly inside
`TracerParticleContainer::Timestamp()`. Quokka's tracer integration calls
advection, redistribution, output, and checkpoint routines, but contains no
`Timestamp()` call, either in the world context or inside a pushed
`ParallelContext` sub-communicator.

Merely constructing an `AmrTracerParticleContainer` does not execute the
timestamp writer schedule or its mixed communicator operations. With the only
affected entry point unused, Quokka cannot form the inconsistent rank/size
pair or enter the potentially deadlocking sends and reduction.

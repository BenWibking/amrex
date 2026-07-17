# Async output is incompatible with particle pre/post mode

## Severity

High

## Affected code

`CheckpointPre`, `CheckpointPost`, and `WriteBinaryParticleData` in
`Src/Particle/AMReX_ParticleIO.H`, plus
`WriteBinaryParticleDataAsync` in
`Src/Particle/AMReX_WriteBinaryParticleData.H`.

## Explanation

The pre/post protocol expects the synchronous writer to populate
`HdrFileNamePrePost`, per-level `which`, `count`, `where`, output-file count,
and file prefixes. The async dispatcher bypasses that implementation and never
initializes those fields.

`CheckpointPost` still runs when `usePrePost` is true. It opens the unset header
name, reduces empty tables, and on the I/O rank ultimately detects the failed
stream and aborts. Meanwhile the async task independently writes a complete
header, so the two protocols have no coherent ownership or ordering.

## Proposed patch

Make the modes explicitly exclusive with an early always-on diagnostic, or
implement an async-aware pre/post protocol whose completion callback owns final
header generation. A minimal safe fix is for pre/post hooks to bypass
themselves when async output is selected and for the async writer to remain the
sole header owner.

Add a regression enabling both settings and require either a successful
round-trip or the explicit compatibility diagnostic.

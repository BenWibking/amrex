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

## Verification

**Conclusion: Confirmed.**

The async dispatcher bypasses `WriteBinaryParticleDataSync()`, the only writer
that fills `HdrFileNamePrePost`, per-level tables, output-file counts, and
prefixes. `CheckpointPost()` nevertheless runs solely from the instance
`usePrePost` flag and opens/reduces those unset fields, while the queued async
task independently owns its complete header.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

The failure requires both `amrex.async_out = 1` and
`particles.use_prepost = 1`. Quokka currently rejects asynchronous output with
an explicit fatal check during initialization, neither option is enabled in a
checked-in Quokka input, and pre/post particle I/O defaults to false. Ordinary
Quokka checkpoint and plotfile paths therefore use the synchronous writer and
do not combine the incompatible state machines.

The conclusion is not absolute because Quokka forwards both AMReX parameters,
and its initialization sequence can write an initial checkpoint before it
reaches the asynchronous-output abort. A user who deliberately enables both
unsupported settings together with initial checkpoint output could still
reach the broken combination. That narrow pre-abort/unsupported route is why
the report is likely rather than definitely not applicable.

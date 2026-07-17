# Selective checkpoints cannot be restarted by the same container type

## Severity

High

## Affected code

The mask-taking `ParticleContainer_impl::Checkpoint` overload and
`ParticleContainer_impl::Restart` in `Src/Particle/AMReX_ParticleIO.H`.

## Explanation

The checkpoint API explicitly permits components to be toggled off. The
writer records only selected components in the header and particle records.
`Restart`, however, requires `header.num_real` and `header.num_int` to equal
the container's complete struct-plus-array component counts.

Thus any actual deselection produces a file that the same particle-container
type rejects before reading. This is unavoidable for omitted struct fields and
also affects runtime/array fields, despite the API describing the output as a
checkpoint suitable for restart.

## Proposed patch

Choose and enforce one contract. Either disallow deselection for checkpoints
and reserve masks for plot output, or extend the format with component identity
and teach restart to map present fields while initializing explicitly omitted
fields. Do not silently create a nominal checkpoint that the matching reader
cannot consume.

Add a round-trip test that omits one permitted component, or a death test for a
new explicit writer-side rejection if selective restart remains unsupported.

## Verification

**Conclusion: Confirmed.**

The mask-taking `Checkpoint` overload records only the selected component
counts in the header. The matching `Restart` implementation then requires
those counts to equal the container's complete compile-time and runtime real
and integer component counts. Thus a checkpoint that legitimately deselects
any component is written successfully but is rejected by restart.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka's checkpoint wrappers do not request selective component output. Each
physics-particle descriptor supplies the complete generated real and integer
name vectors, and the selected AMReX overload constructs all-enabled masks;
tracers use the default complete checkpoint overload. Files produced by these
paths contain the full component counts that Quokka's later `Restart` expects,
so current Quokka checkpoint/restart round trips avoid the format mismatch.

Selective masks are nevertheless part of the underlying AMReX API and could
be adopted for smaller Quokka diagnostics or checkpoint policies. Because
Quokka both writes and restarts particle data through the affected library,
such a change would expose the incompatibility. No present call deselects a
component, making impact unlikely rather than impossible.

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

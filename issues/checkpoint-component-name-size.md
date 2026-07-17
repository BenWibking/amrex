# Checkpoint indexes caller component names before validating their lengths

## Severity

High

## Affected code

The component-name `ParticleContainer_impl::Checkpoint` overload in
`Src/Particle/AMReX_ParticleIO.H`.

## Explanation

If a name vector is nonempty, the wrapper indexes it while constructing its
temporary name list. It does not first require the vector to contain exactly
all output components. A short real or integer vector is therefore read out of
bounds before control reaches `WriteBinaryParticleDataSync`, whose later name
length assertion is too late.

The failure is particularly easy to trigger when runtime components have been
added after a call site prepared its name list.

## Proposed patch

Before either loop, require each name vector to be empty or to have the exact
expected layout-dependent length. Use checked indexing while constructing the
temporary vectors and provide a message that reports expected and actual
sizes.

Add tests for empty/default names, exact names, and undersized real/int lists
on hybrid and pure-SoA containers.

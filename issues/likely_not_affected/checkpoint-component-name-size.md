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

## Verification

**Conclusion: Confirmed.**

The component-name `Checkpoint` overload in
`Src/Particle/AMReX_ParticleIO.H` indexes
`real_comp_names[i-first_rcomp]` and `int_comp_names[i]` for every expected
component whenever the corresponding vector is nonempty. It performs no
exact-size check before those loops, so an undersized nonempty name vector is
read out of bounds.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka directly calls this named `Checkpoint` overload, but its
`PhysicsParticleRegister` derives each real and integer name vector from the
same compile-time particle traits that define the container. Fixed layouts
use their enum names, and radiation/star layouts expand the final name to the
trait's complete component count. Tracers use the simpler overload without
custom name vectors. The checked-in combinations therefore provide either an
empty vector or an exact-length vector, not the undersized nonempty input that
triggers the out-of-bounds read.

The classification is not definite because Quokka's star model and radiation
group counts are extensible template inputs, and this affected overload is on
every physics-particle checkpoint path. A future naming-trait mismatch would
immediately expose the AMReX bug. Current generation logic keeps the sizes in
lockstep, so the issue is unlikely to affect present Quokka configurations.

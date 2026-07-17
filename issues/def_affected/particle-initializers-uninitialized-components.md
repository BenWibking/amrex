# File and random initializers leave unspecified components uninitialized

## Severity

High

## Affected code

Initialization routines in `Src/Particle/AMReX_ParticleInit.H`, especially
`InitFromAsciiFile`, `InitFromBinaryFile`, and the `ParticleInitData`-based
random/cell initializers when runtime components exist.

## Explanation

The file APIs allow fewer data fields than the container owns, but their local
`ParticleType` objects and newly resized SoA entries are not value-initialized.
The remaining struct/array fields therefore contain indeterminate values.

The `ParticleInitData` structure covers compile-time components only. If
runtime components have been added, the random and per-cell routines neither
accept values for them nor initialize them to a documented default. Subsequent
redistribution and output can communicate or serialize those uninitialized
bytes.

## Proposed patch

Define a consistent missing-field policy, preferably zero initialization, and
apply it to every particle/tile before filling supplied data. For runtime
components, either accept explicit initialization vectors or initialize them
deterministically and document that behavior.

Add tests that initialize only a prefix of file fields and that call each
`ParticleInitData` routine after adding runtime real/int components.

## Verification

**Conclusion: Confirmed.**

File readers default-construct `ParticleType` and fill only the requested
prefix, while POD tile resize does not value-initialize all remaining entries.
The `ParticleInitData` paths populate only compile-time fields and have no
runtime-field input or zero-fill step. Unspecified struct, array, and runtime
bytes can consequently be redistributed or serialized.

## Quokka impact classification

**Classification: Definitely affects Quokka.**

Quokka directly uses the affected prefix-reading behavior in several shipped
particle problems. Its sink particle type has eight AoS real components
(`mass`, three velocities, `mdot`, and three angular-momentum components), but
`ParticleAccretion`, `ParticleSink`, and `ParticleSinkSubcycle` call
`InitFromAsciiFile` with only four extra fields. Likewise, `TallBoxSf` reads
only seven fields into a stochastic-stellar-population type with at least
fourteen real components, and `ParticleStarEvolution` reads five fields into a
larger star-particle layout. These are ordinary checked-in initialization
paths, not invalid inputs or optional AMReX layouts.

The omitted fields are also semantically live in Quokka. Sink accretion and
output use `mdot` and angular momentum, while stellar evolution, feedback, and
diagnostics use birth/death metadata, mass-at-birth, radius, and luminosity
fields. AMReX leaves those omitted AoS fields indeterminate before Quokka has a
chance to update every one of them, so their values can enter redistribution,
checkpoint output, or physics calculations. Because current Quokka call sites
satisfy the exact trigger without requiring a nondefault runtime option, this
belongs in the definite rather than likely category.

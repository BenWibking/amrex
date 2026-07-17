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

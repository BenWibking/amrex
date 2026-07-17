# Runtime-only particle tiles accept negative sizes and component counts

## Severity

High

## Affected code

`ParticleTileRT::define`, `resize`, `reserve`, and `realloc_and_move` in
`Src/Particle/AMReX_ParticleTileRT.H`.

## Explanation

Unlike classic particle tiles, the runtime-only tile uses signed `Long` for
particle sizes and signed `int` for component counts. These public entry points
do not reject negative values.

A negative `resize` value can be stored directly in `m_size` when no growth is
needed, making later loops and range arithmetic invalid. Negative component
counts or capacities flow into allocation expressions such as
`new_capacity * new_n_real`; conversion to an allocation size can request an
enormous buffer or overflow. A negative reserve increment can also corrupt
capacity calculations through the static batch helper.

## Proposed patch

Add always-on nonnegative checks at the public `define`, `resize`, and `reserve`
boundaries, and assert the invariant again in `realloc_and_move`. Validate batch
reserve additions before summing them with existing sizes.

Add tests that reject every negative input and verify zero values leave a
defined, empty tile with nonnegative size/capacity/component counts.

## Verification

**Conclusion: Confirmed.**

The public runtime-tile `define`, `resize`, and `reserve` paths accept signed
counts without nonnegative validation. Negative values are stored in tile
state and flow into allocation arithmetic and copy/move ranges, breaking the
class's size and capacity invariants.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka never constructs `ParticleTileRT` and never calls its runtime `define`,
`resize`, `reserve`, or `realloc_and_move` functions. Its
`AmrParticleContainer` tiles have compile-time AoS particle types and use the
standard `ParticleTile` size interface with nonnegative counts derived from
particle creation or file records.

The signed RT tile state at issue does not exist in a Quokka executable.
Introducing runtime-only particles would be a new feature, so current Quokka
is definitely unaffected.

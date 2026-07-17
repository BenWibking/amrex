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


# Particle filter helpers dereference before empty masks

## Severity

High

## Affected code

The scan-based `filterParticles` and single-/two-destination
`filterAndTransformParticles` implementations in
`Src/Particle/AMReX_ParticleTransformation.H`.

## Explanation

After allocating an offsets vector and scanning the mask, each implementation
computes the output count by copying the final mask and offset entries. For an
empty input (`n == 0` or `src.numParticles() == 0`), those expressions are
`mask - 1` and `offsets.data() - 1`.

The invalid device/host copy occurs before the later kernel and stream
synchronization, so filtering an empty tile can crash, trigger a GPU memory
fault, or return a meaningless count. Predicate convenience overloads also
reach these implementations after creating a zero-length mask.

## Proposed patch

Return zero immediately before allocating/scanning/copying when the requested
count is zero. Apply the guard to the shared lowest-level implementations so
all mask and predicate overloads inherit it.

Add empty-tile tests for mask and predicate forms, with one and two
destinations, on CPU, OpenMP, and GPU configurations.

## Verification

**Conclusion: Confirmed.**

The shared filtering implementations read `mask[n-1]` or `offsets[n-1]` after
the scan without guarding `n == 0`. Empty source tiles reach these helpers
through both predicate and mask APIs, producing an immediate negative-index
read.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka has no direct `filterParticles` or `filterAndTransformParticles` call.
The AMReX base-container clients of the affected empty-range helpers are
`addParticles`, virtual/ghost creation, and async output. Quokka uses none of
the first three operations; its unsupported async writer only calls the
predicate filter after verifying that a grid has a positive valid-particle
count, so it does not pass an empty source range here.

Quokka's normal destruction path invalidates IDs and calls `Redistribute`
without these scan helpers. No current call can supply `n == 0` to the faulty
implementation, making the exclusion definite.

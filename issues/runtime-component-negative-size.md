# Runtime component resize accepts negative counts

## Severity

High

## Affected code

`ResizeRuntimeRealComp` and `ResizeRuntimeIntComp` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

Both public APIs assign `new_size` directly to the container count and use it
in a vector-resize expression without checking `new_size >= 0`. A negative
value can become a huge unsigned allocation request or leave the communication
mask, reported component count, and tile layout mutually inconsistent.

The eventual symptom may be an allocation exception, a failed assertion, or
out-of-bounds component access rather than a useful input error.

## Proposed patch

Reject negative counts with an always-on assertion before mutating any member.
Compute resize lengths in a checked signed type, then commit all related state
only after validation succeeds.

Add negative-count death tests and verify that a rejected call leaves the
container unchanged.

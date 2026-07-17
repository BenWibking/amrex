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

## Verification

**Conclusion: Confirmed.**

Both resize functions assign the signed `new_size` to container state before
using it in vector-size arithmetic, with no nonnegative check. A negative count
can convert to a huge allocation request after the reported component count
and runtime-defined flag have already been mutated.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This failure requires a call to `ResizeRuntimeRealComp()` or
`ResizeRuntimeIntComp()` with a negative component count. Quokka never adds or
resizes AMReX runtime particle components; its particle payloads are fixed by
the compile-time `AmrParticleContainer<NReal, NInt>` aliases.

All current Quokka containers therefore retain zero runtime-defined SoA
components, and no user input is forwarded to either resize API. With the
mutators absent from the call graph, a negative value cannot reach the signed-
to-size conversion described by the issue.

# Runtime component resize leaves the name vectors out of sync

## Severity

Medium

## Affected code

`ResizeRuntimeRealComp` and `ResizeRuntimeIntComp` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

The public resize functions change `m_num_runtime_real` or
`m_num_runtime_int`, communication masks, particle size, and tile layouts, but
they never resize the corresponding `m_soa_rdata_names` or
`m_soa_idata_names` vector.

Growing creates components with no names; shrinking leaves names for
nonexistent components. Name-based lookup, checkpoint metadata, and
`make_alike` can then disagree with the actual number and ordering of arrays,
leading to out-of-range access or incorrect component identity.

## Proposed patch

Resize the name vector in lockstep with the runtime component count. Preserve
surviving names, generate collision-free defaults for new entries, and erase
removed names. Consider routing both resize and single-component add through
one implementation so counts, names, masks, and tiles cannot diverge.

Add grow/shrink tests that cover lookup by name, checkpoint metadata, and
`make_alike`.

## Verification

**Conclusion: Confirmed.**

`ResizeRuntimeRealComp()` and `ResizeRuntimeIntComp()` update component counts,
communication masks, and tile storage but never touch `m_soa_rdata_names` or
`m_soa_idata_names`. Growing leaves fewer names than arrays; shrinking leaves
stale names that `make_alike()`, metadata, and name lookup still traverse.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

The metadata desynchronization occurs only after
`ResizeRuntimeRealComp()` or `ResizeRuntimeIntComp()` changes an already
defined runtime SoA layout. Quokka does not call either resize function, nor
the corresponding runtime-component add APIs; its particle component counts
and meanings are fixed by compile-time container aliases.

Quokka therefore has no mutable runtime component-name vectors to diverge
from tile storage. Operations such as checkpointing or making temporary
containers see the unchanged, compile-time layout and cannot encounter the
grow/shrink mismatch described here.

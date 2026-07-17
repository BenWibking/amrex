# sumNeighbors accepts invalid component ranges

## Severity

High

## Affected code

`NeighborParticleContainer_impl::sumNeighbors`, `sumNeighborsCPU`, and
`sumNeighborsMPI` in `Src/Particle/AMReX_NeighborParticlesI.H` and
`Src/Particle/AMReX_NeighborParticlesCPUImpl.H`.

## Explanation

The public API accepts signed start components and signed counts without any
validation. Those values drive direct component access in
`getSummedRealComp`/`getSummedIntComp` and buffer-size expressions such as:

```cpp
old_size + real_num_comp*sizeof(ParticleReal)
         + int_num_comp*sizeof(int)
```

A negative start indexes before struct or array component storage. An endpoint
beyond the available struct-plus-SoA components indexes past runtime pointer
arrays. Negative counts are converted to unsigned during size arithmetic and
can request an enormous allocation; they can also make the receive record
size zero or wrapped, breaking division and modulo checks.

## Proposed patch

Before dispatch, require nonnegative starts/counts and use checked endpoint
tests against `NStructReal + NumRealComps()` and `NStructInt + NumIntComps()`
(with the correct pure-SoA indexing convention). Reject addition overflow when
forming each endpoint and byte record size.

Add tests for empty valid ranges, full valid ranges, negative starts/counts,
and one-past-end endpoints for both real and integer components.

## Verification

**Conclusion: Confirmed.**

The public `sumNeighbors()` forwards all four signed values without checking
them. The CPU helpers use the starts as direct component indices and the counts
in byte-size arithmetic, so negative or overlong ranges reach invalid pointer
arrays and negative counts can wrap allocation sizes.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

`sumNeighbors()` is an operation of AMReX's neighbor-particle container and
the unchecked component ranges are consumed by that subsystem's neighbor
reduction helpers. Quokka neither instantiates `NeighborParticleContainer`
nor calls `sumNeighbors()`; its particle exchange uses the standard
`AmrParticleContainer` redistribution path.

Accordingly, Quokka never supplies source/destination starts or counts to the
affected API. There is no alternate path from its normal particle operations
into these helpers, so malformed ranges cannot reach the invalid pointer or
allocation arithmetic.

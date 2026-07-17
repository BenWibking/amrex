# Binary initialization asserts when a reader has no particles left

## Severity

High

## Affected code

The redistribution loop in `ParticleContainer_impl::InitFromBinaryFile` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

Every selected reader enters every globally computed redistribution round and
asserts `MyCnt > how_many_read`. Some readers legitimately receive zero
particles when `NP < NReaders`. More generally, readers with the floor-sized
share can finish one round before the final reader that owns the remainder.

Those exhausted readers then fail the assertion in debug builds even though a
zero-length contribution is valid. Optimized builds happen to compute
`NRead == 0`, making behavior depend on assertion settings.

## Proposed patch

If `how_many_read >= MyCnt`, let that reader contribute an empty batch and
continue participating in the required collectives. Compute `NRead` only from
a nonnegative remaining count and remove the invalid strict assertion.

Add cases with fewer particles than readers and with a remainder that requires
one extra global batch.

## Verification

**Conclusion: Confirmed.**

Every chosen reader enters every globally computed redistribution round and
asserts `MyCnt > how_many_read`. Readers assigned zero particles, and readers
whose smaller share finishes before the remainder owner, legitimately have
equality and fail this assertion even though their required contribution is an
empty batch.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This assertion is reached only by AMReX `InitFromBinaryFile`'s distributed raw
particle reader. Quokka never calls that initializer; its file-backed initial
particles use `InitFromAsciiFile`, and its checkpoint input uses the distinct
AMReX `Restart` implementation. Neither path executes the binary reader's
`MyCnt` batching loop or reader-share assertion.

MPI rank counts and uneven Quokka particle files therefore cannot trigger this
specific failure. A new raw-binary initialization feature would have to be
added to Quokka first, making the current non-impact definite.

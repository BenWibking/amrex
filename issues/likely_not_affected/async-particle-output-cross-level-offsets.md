# Async particle offsets mix separate AMR level files

## Severity

High

## Affected code

The `np_on_rank` and `rank_start_offset` calculation in
`WriteBinaryParticleDataAsync` in
`Src/Particle/AMReX_WriteBinaryParticleData.H`.

## Explanation

Async particle data are written to separate paths for each AMR level, such as
`Level_0/DATA_00000` and `Level_1/DATA_00000`. Offsets in the particle header
must therefore start from zero independently in each level file and account
only for data written earlier to that same file.

The implementation first accumulates `np_on_rank[rank]` over every level and
then builds one `rank_start_offset[rank]` vector. It reuses those offsets while
writing the header entries for every level. A rank's starting offset in a
level-N file therefore includes particle bytes attributed to previous ranks
on all other levels, even though those bytes live in different files.

For a multi-level output with more than one rank sharing an output file, the
header can point beyond the actual grid data. Restart or plotfile readers then
seek to the wrong location and read truncated or unrelated particle records.

## Proposed patch

Track particle totals and starting offsets per level and per rank. For each
level, compute a fresh prefix within each async output-file group using only
that level's counts, and use that level-specific prefix when emitting grid
header entries.

Add an async-output round-trip test with at least two AMR levels and two ranks
assigned to the same output file, with different nonzero counts on each level.
Verify every recorded offset and the reloaded particle data.

## Verification

**Conclusion: Confirmed.**

`np_on_rank` is accumulated across every AMR level and used to form one
`rank_start_offset`. Header rows for every level reuse that prefix, even though
the writer opens an independent `Level_N/DATA_*` file per level. An offset for
a later rank therefore includes bytes belonging to other level files.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

Quokka commonly has multilevel particle hierarchies, so the data shape needed
for this defect is realistic. The missing ingredient in supported runs is the
async writer: Quokka rejects `amrex.async_out = 1`, and its repository inputs
leave the option disabled. Synchronous particle output computes offsets per
level and is not affected by this async-only accumulation error.

The issue is not definitely irrelevant because Quokka passes the AMReX async
option through and may write an initial checkpoint before its later fatal
async guard. A deliberately unsupported GPU or CPU run with async output,
particles on more than one AMR level, and initial checkpointing could generate
bad offsets before termination. Existing supported workflows avoid the path,
but Quokka's multilevel data makes the latent path credible enough to retain
the likely-not qualifier.

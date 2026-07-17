# Binary metafile initialization can silently skip its input

## Severity

Medium

## Affected code

`ParticleContainer_impl::InitFromBinaryMetaFile` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

The routine never checks whether the metafile opened successfully, so a
missing or unreadable file returns as a successful no-op. Its loop also calls
`getline` and then tests `ifs.good()` before processing the extracted string.
If the last valid pathname is not newline-terminated, `getline` sets `eofbit`
after extracting it and the routine discards that final entry.

Blank lines are passed through as empty binary filenames as well.

## Proposed patch

Validate the initial stream and iterate with `while (std::getline(ifs, file))`.
Process a successfully extracted final line regardless of `eofbit`, and define
a clear blank/comment-line policy. Check stream failure again after the loop to
distinguish EOF from an I/O error.

Add missing-file, no-final-newline, blank-line, and multi-entry tests.

## Verification

**Conclusion: Confirmed.**

`InitFromBinaryMetaFile` in `Src/Particle/AMReX_ParticleInit.H` neither checks
that the metadata stream opened nor processes a line unless `ifs.good()`
remains true after `std::getline`. A successfully extracted final line without
a trailing newline sets `eofbit`, fails that test, and is skipped; a missing
file likewise becomes an unexplained no-op.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka has no call to `InitFromBinaryMetaFile` and does not accept this AMReX
metafile format in any checked-in problem or restart helper. Initial particle
files are read with `InitFromAsciiFile`; simulation checkpoints are loaded by
`ParticleContainer::Restart` from Quokka's checkpoint directory. Those paths
open different files and do not execute the faulty metafile `getline` loop.

Because the affected API and file format are absent from Quokka's input
surface, neither a missing trailing newline nor a failed metafile open can
alter a Quokka run. This is a definite call-path exclusion.

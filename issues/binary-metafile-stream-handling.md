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

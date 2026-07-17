# GPU neighbor fill silently ignores refined levels

## Severity

High

## Affected code

`buildNeighborMask` and `buildNeighborCopyOp` in
`Src/Particle/AMReX_NeighborParticlesGPUImpl.H`.

## Explanation

Both routines hard-code `const int lev = 0` and build code arrays and copy
operations only for that level. `fillNeighborsGPU` has no single-level
precondition and is exposed through the same multi-level neighbor-container
interface as the CPU implementation.

For a container with particles on levels 1 or higher, the GPU fill clears
neighbor counts on every level but repopulates only level 0. Same-level copies
on refined levels and all cross-level neighbor relationships are absent. The
operation completes without an error, so downstream neighbor lists silently
omit interactions.

The boundary-ID storage is also initialized only with `resize(1)`, reinforcing
the unannounced level-0-only assumption.

## Proposed patch

Either implement GPU mask/copy-plan construction for every defined level,
including cross-level mappings consistent with the CPU path, or add an
always-on single-level rejection before any neighbor state is cleared. Do not
silently accept a hierarchy the implementation cannot fill.

Add a two-level GPU regression with particles requiring refined-level and
cross-level neighbor copies. If the feature remains unsupported, test the
explicit diagnostic instead.

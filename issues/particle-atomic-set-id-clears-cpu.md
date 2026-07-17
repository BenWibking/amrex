# Particle::atomicSetID clears the particle CPU field

## Severity

High

## Affected code

`Particle::atomicSetID` in `Src/Particle/AMReX_Particle.H`.

## Explanation

The packed `m_idcpu` word stores the 40-bit signed ID in its upper bits and the
24-bit CPU of origin in its lower bits. `atomicSetID` creates a temporary word
initialized to zero, packs only the new ID into it, and atomically writes the
entire temporary word to `m_idcpu`.

As a result, setting an ID also overwrites the CPU field with zero. On every
rank other than zero, this corrupts the `(id, cpu)` pair that AMReX documents as
the particle's globally unique identity. Existing test coverage verifies that
the ID changed and changed back, but does not check that `cpu()` survived.

## Proposed patch

Build the replacement word with the existing CPU bits preserved. If CPU is
immutable while an ID update is in flight, initialize the temporary from
`m_idcpu` and call `pack_id` before the atomic exchange. If concurrent CPU
updates are permitted, use a compare-exchange loop that replaces only the ID
bits while retaining the latest low 24 bits.

Extend the atomic-ID test to create particles whose CPU field is nonzero, call
`atomicSetID` on CPU/OpenMP/GPU paths, and verify both the new ID and the
unchanged CPU after every update.


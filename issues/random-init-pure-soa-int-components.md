# Pure-SoA random initialization skips integer components zero and one

## Severity

High

## Affected code

Both serialized and parallel branches of
`ParticleContainer_impl::InitRandom` in
`Src/Particle/AMReX_ParticleInit.H`.

## Explanation

In the pure-SoA branch, the code stages integer array data with a loop starting
at index 2. Pure-SoA ID and CPU are held in the separate packed `m_idcpu`
array; `NArrayInt` contains only user integer components, so indices zero and
one are not reserved.

The destination tile is resized for all integer arrays, but the first two host
vectors remain empty and copy no values. Those particle entries are left
uninitialized while later components receive the requested `ParticleInitData`.

## Proposed patch

Start the pure-SoA integer loop at zero in both branches. Add a shared helper so
serialized and parallel initialization cannot drift.

Add a pure-SoA regression with at least two integer components and verify every
particle against `pdata.int_array_data`.

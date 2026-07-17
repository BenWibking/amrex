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

## Verification

**Conclusion: Confirmed.**

Both pure-SoA branches stage user integer arrays with `for (int i = 2; i <
NArrayInt; ++i)`. ID and CPU are stored separately in `m_idcpu`; they do not
occupy `NArrayInt` slots. User components zero and one therefore retain
uninitialized resized storage.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This indexing error exists only in the pure-SoA branches of AMReX's random
particle initializers, where user integer components live in `NArrayInt`
arrays beside separately packed ID/CPU data. Quokka's particle aliases place
all compile-time user integer components in the AoS particle struct and define
zero pure-SoA integer arrays.

Consequently, Quokka's `InitRandom()` use selects the AoS staging loop rather
than the `i = 2` pure-SoA loop. The presence of integer particle fields in
Quokka does not create exposure: their storage category is different, and the
faulty array range is not instantiated for those containers.

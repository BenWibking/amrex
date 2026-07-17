# Particle I/O options are applied only to the first container instance

## Severity

Medium

## Affected code

`ParticleContainer_impl::Initialize` in
`Src/Particle/AMReX_ParticleContainerI.H`.

## Explanation

`usePrePost` and `doUnlink` are instance members. `Initialize` resets them to
`false` and `true`, respectively, for every newly constructed container, but
the `ParmParse` queries that override those defaults are inside a
function-local `static bool initialized` guard.

Consequently, only the first container of a given template specialization
receives `particles.use_prepost` and `particles.do_unlink`. Later containers
silently retain the hard-coded defaults, so two otherwise identical particle
containers can take different checkpoint paths and unlink behavior in the
same run.

The guard is appropriate for genuinely static settings such as `do_tiling`,
but not for values stored on each object.

## Proposed patch

Query `use_prepost` and `do_unlink` for every instance, outside the static
initialization block. Alternatively, cache their parsed values in static
locals and copy those values to each instance after the one-time parse.

Add a regression that constructs two containers after setting both parameters
away from their defaults and verifies that both accessors report the same
configured values.

## Verification

**Conclusion: Confirmed.**

Particle-container initialization resets the instance members `usePrePost`
and `doUnlink` to their defaults, but queries `particles.use_prepost` and
`particles.do_unlink` only inside a function-local one-time `initialized`
guard. Consequently only the first constructed instance receives configured
values; later instances retain the defaults.

## Quokka impact classification

**Classification: Likely does not affect Quokka.**

With Quokka's current defaults, `use_prepost` is false and `do_unlink` is true,
which are exactly the values assigned to every later container; the one-time
parse bug is therefore behaviorally invisible. Quokka can register several
particle types, but they generally have different `Particle<NReal,NInt>`
specializations, so each specialization gets its own one-time initialization.
Temporary analysis containers can repeat a specialization, but they only
redistribute data and do not perform particle checkpoint I/O.

This remains a likely rather than definite exclusion because Quokka forwards
both AMReX parameters and supports multiple/extensible particle types. A user
who selects nondefault I/O options and introduces two live containers with the
same specialization could observe inconsistent behavior. No checked-in
problem has that combination, making current impact unlikely.

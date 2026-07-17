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

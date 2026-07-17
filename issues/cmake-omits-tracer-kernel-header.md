# CMake installation omits the tracer-particle kernel header

## Severity

Medium

## Affected code

`Src/Particle/CMakeLists.txt`, compared with `Src/Particle/Make.package` and
`AMReX_TracerParticles.cpp`.

## Explanation

`AMReX_TracerParticles.cpp` includes `AMReX_TracerParticle_mod_K.H`, and the GNU
Make package explicitly lists that header. The particle CMake target-source
list contains every other particle header but omits this one.

AMReX derives the CMake target's `PUBLIC_HEADER` install set from its listed
sources. The source-tree build succeeds because the particle include directory
is available, but a CMake installation does not install the tracer kernel
header. This makes the CMake and GNU Make package surfaces inconsistent and can
break downstream code that includes the kernel header directly.

## Proposed patch

Add `AMReX_TracerParticle_mod_K.H` to the particle `target_sources` list beside
`AMReX_TracerParticles.H`. If the header is intentionally private, make that
policy explicit and remove it from the GNU Make public header set rather than
letting the two build systems drift.

Add an install-tree check that compares particle public headers across CMake
and GNU Make packaging and compiles a small consumer against the installed
tree.

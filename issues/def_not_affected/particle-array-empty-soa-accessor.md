# Empty SoA ParticleArray forms pointers through element zero

## Severity

Medium

## Affected code

SoA `DataLayoutPolicy::get_raw_data_impl` in
`Src/Particle/AMReX_ParticleArray.H`.

## Explanation

`ParticleArray::get_particle_data()` is valid for an empty array and should
return a zero-length accessor. In the SoA policy, however, the raw pointers are
formed as

```cpp
&std::get<Is>(a_container)[0]
```

for every component. When the component vectors are empty, indexing element
zero is outside the vector and is undefined behavior even if the resulting
accessor is never dereferenced. A default-constructed `ParticleArray` is
explicitly resized to zero, so this is a normal state rather than an exotic
corruption case.

## Proposed patch

Obtain the storage pointer with each container's `data()`/`dataPtr()` operation,
which is defined for an empty vector and yields a null or non-dereferenceable
sentinel pointer without indexing an element.

Add CPU and GPU tests that default-construct and explicitly zero-size an SoA
`ParticleArray`, obtain its accessor, and verify `size() == 0` under
undefined-behavior sanitization where available.

## Verification

**Conclusion: Confirmed.**

The SoA `DataLayoutPolicy::get_raw_data_impl()` forms each component pointer
with `&vector[0]`. Default construction and `resize(0)` leave those vectors
empty, making the accessor itself undefined before a caller dereferences the
returned pointer.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

This accessor belongs to the SoA `ParticleArray` layout policy. Quokka's
particle aliases use AoS `Particle<NReal,NInt>` storage and do not instantiate
the SoA `DataLayoutPolicy` or call its raw-data accessor. Empty Quokka particle
tiles still expose AoS/tile-data interfaces, not `&vector[0]` for SoA
component vectors.

Because the faulty template branch is not instantiated by any Quokka particle
type, empty particle populations cannot reach this undefined expression.
Adopting pure-SoA particles would be a new type-level change, so non-impact is
definite.

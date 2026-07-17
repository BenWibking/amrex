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


# Nearest particle interpolation is shifted by half a cell

## Severity

High

## Affected code

`ParticleInterpolator::Nearest::Nearest` in
`Src/Particle/AMReX_ParticleInterpolators.H`.

## Explanation

The constructor computes

```cpp
l = (p.pos(i) - plo[i]) * dxi[i] + 0.5;
index[i] = floor(l);
```

For the cell-centered meshes used by `ParticleMesh`, a particle at the center
of cell `i` has normalized coordinate `i + 0.5`. The code adds another half and
therefore selects cell `i + 1` at the exact center. More generally, its
selection boundary lies at cell centers rather than at cell faces, so roughly
half of each cell deposits to or samples from the adjacent cell.

The `Linear` interpolator's extra half-cell is paired with a `-1` base-index
adjustment and is consistent with cell-centered CIC weights. `Nearest` has no
corresponding adjustment.

## Proposed patch

For cell-centered nearest-cell interpolation, compute the index as
`floor((pos - plo) * dxi)`. If nodal nearest-grid-point behavior is also
required, make centering an explicit constructor policy instead of using one
formula ambiguously for both layouts.

Add tests with particles at cell centers and on both sides of cell faces,
checking particle-to-mesh deposition and mesh-to-particle sampling in every
dimension, including periodic boundary cells.


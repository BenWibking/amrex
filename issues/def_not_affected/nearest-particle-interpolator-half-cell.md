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

## Verification

**Conclusion: Confirmed.**

`NearestParticleInterpolator` computes `floor(q + 0.5)`, but its particle-mesh
wrappers use cell-centered arrays. At a cell center `q == i + 0.5`, the formula
selects `i + 1` rather than `i`. The linear interpolator's adjacent formula
contains the required cell-centered shift, confirming the centering convention
used by this API.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

Quokka does not instantiate `ParticleInterpolator::Nearest`. Its deposition
code defines and uses a distinct `NearestEight` interpolator for eight-cell
coupling and uses AMReX's linear interpolation for CIC-style deposition and
mesh-to-particle gathering. These algorithms have different weights and index
construction and do not call the single-cell `Nearest` constructor.

Consequently Quokka particle positions never pass through the extra-half-cell
formula identified here. Replacing a Quokka interpolation policy with
`Nearest` would be a new code change, so present impact is definitely absent.

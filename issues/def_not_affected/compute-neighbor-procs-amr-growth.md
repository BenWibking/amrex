# Local particle redistribution can omit AMR-level neighbor ranks

## Severity

High

## Affected code

`computeNeighborProcs` in `Src/Particle/AMReX_ParticleUtil.cpp`.

## Explanation

`computeNeighborProcs` converts each source-level box into the index space of
every possible destination level, then grows the converted box to find all MPI
ranks that a local redistribution may contact. The grow distance is currently
scaled with

```cpp
computeRefFac(a_gdb, 0, src_lev) * ngrow
```

even though `box` is already expressed in destination-level (`lev`) indices.
That mixes index spaces. In particular, converting a level-0 box to a finer
level refines the box but still grows it by only `ngrow` fine cells. A particle
that is allowed to move `ngrow` level-0 cells can therefore reach fine grids
farther away than the neighbor-rank set includes.

`ParticleCopyPlan::build` uses this set for the local MPI handshake. If the
omitted fine grid belongs to another rank, that rank does not post the matching
exchange, which can lose a particle transfer or hang communication. The
reverse direction is over-conservative rather than compensating for the
missing finer-level ranks.

## Proposed patch

Express the allowed source-level displacement in the destination level's index
space before growing the converted box. Factor the conversion into a helper
that handles both refinement and coarsening (rounding coarse-level growth up),
rather than multiplying by the source level's cumulative refinement ratio.

Add a multi-level MPI regression in which a particle begins on a coarse grid,
moves within the documented `local` distance into a fine patch owned by a rank
that is outside the current under-grown intersection, and verify that local
`Redistribute` transfers it to that rank.

## Verification

**Conclusion: Confirmed.**

`computeNeighborProcs()` first converts `src_box` into destination-level
indices, then grows that converted box by `computeRefFac(0, src_lev)*ngrow`.
The growth is therefore still scaled for the source level. A finer destination
is under-grown, so the rank set used by the local particle handshake can omit
a reachable fine-grid owner.

## Quokka impact classification

**Classification: Definitely does not affect Quokka.**

`computeNeighborProcs` is consulted only by the optimized local
`Redistribute` handshake, selected when the fourth `Redistribute` argument is
positive. Every Quokka redistribution call uses the global algorithm: calls
with `(lev, finestLevel(), ngrow)` pass `ngrow` as the third retention-width
argument and leave the fourth local-distance argument at its zero default.
Post-regrid, initialization, restart, and destruction paths do the same.

As a result Quokka builds an all-rank copy plan and never consumes the
under-grown neighbor-rank set. Changing AMR ratios or particle displacement
cannot activate this function without first changing Quokka to local
redistribution, so the current exclusion is definite.

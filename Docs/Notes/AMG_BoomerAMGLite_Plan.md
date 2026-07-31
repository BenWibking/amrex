# Minimal BoomerAMG-Like Solver for AMReX

Companion mathematical specification:
[AMG_BoomerAMGLite_Equations.tex](AMG_BoomerAMGLite_Equations.tex).

## 1. Decision

Implement a scalar, classical C/F algebraic multigrid method with one
deliberately narrow configuration:

| Component | AMReX implementation | HYPRE comparison setting |
|---|---|---|
| Strength graph | Classical sign-based strength, threshold \(0.25\), maximum row sum \(0.9\) | `strong_threshold = 0.25`, `max_row_sum = 0.9` |
| Coarsening | PMIS C/F splitting | `coarsen_type = 8` |
| Interpolation | Matrix-based Extended+i, at most four entries per row | `interp_type = 6`, `PMaxElmts = 4` |
| Smoothing | One L1-Jacobi sweep down and up | relax type `18` |
| Restriction | \(R=P^T\) | Default transpose restriction |
| Coarse operator | \(A_c=P^TAP\) | Galerkin |
| Cycle | Multiplicative V(1,1) | `cycle_type = 1` |
| Coarse solve | Dense pivoted LU for at most nine unknowns | coarse relax type `9` |
| Aggregation | None | `agg_num_levels = 0` |

This matches the raw BoomerAMG GPU algorithmic defaults. The native
implementation uses deterministic global-row priorities for PMIS instead of
reproducing HYPRE's random stream, so valid hierarchy details can still
differ even though the configured algorithms match.

The first implementation is intentionally not a framework for arbitrary AMG
strategies. PMIS, Extended+i interpolation, L1-Jacobi, and Galerkin coarsening
are hard-coded until this vertical slice is correct and tested.

## 2. Goals

1. Complete the design started on Weiqun Zhang's
   [`alg`](https://github.com/WeiqunZhang/amrex/tree/alg) branch.
2. Reuse AMReX algebra types and GPU/MPI execution infrastructure.
3. Produce a native AMG preconditioner that does not require HYPRE at runtime.
4. Preserve classical C/F splitting; do not introduce aggregates or aggregate
   IDs.
5. Compare against HYPRE BoomerAMG configured with the same algorithmic
   profile.
6. Pass when both solvers meet the requested true-residual tolerance and their
   solutions agree to the input tolerance on a conditioned manufactured
   problem.

## 3. Initial Scope

The first supported matrix class is:

- square and nonsingular;
- scalar, with value type `amrex::Real`;
- nonzero diagonal;
- positive diagonal with predominantly nonpositive off-diagonal entries;
- representative of scalar elliptic diffusion or shifted Poisson systems.

The first implementation does not include:

- aggregation or aggressive coarsening;
- multipass or energy-minimizing interpolation;
- systems/nodal AMG or multiple functions;
- near-nullspace vectors;
- non-Galerkin dropping;
- nonsymmetric restriction;
- singular/nullspace-aware solves;
- arbitrary interpolation truncation policies or row caps other than the
  configured largest-magnitude cap;
- reuse after the fine matrix values or sparsity pattern change.

These constraints must be checked or documented at the public entry point.

## 4. Existing Infrastructure

The implementation belongs in `Src/LinearSolvers`, beside the current algebra
types, rather than in geometric MLMG.

The reusable pieces are:

- [`AMReX_AlgPartition.H`](../../Src/LinearSolvers/AMReX_AlgPartition.H):
  distributed row-prefix partitions;
- [`AMReX_AlgVector.H`](../../Src/LinearSolvers/AMReX_AlgVector.H):
  distributed vectors, reductions, and BLAS-like updates;
- [`AMReX_SpMatrix.H`](../../Src/LinearSolvers/AMReX_SpMatrix.H):
  distributed CSR storage, local/off-rank splitting, and cached SpMV
  communication;
- [`AMReX_SpMV.H`](../../Src/LinearSolvers/AMReX_SpMV.H):
  CPU and GPU sparse matrix-vector products;
- [`AMReX_SpMatUtil.H`](../../Src/LinearSolvers/AMReX_SpMatUtil.H):
  distributed rectangular transpose;
- [`AMReX_Smoother_MV.H`](../../Src/LinearSolvers/AMReX_Smoother_MV.H):
  the existing Jacobi smoother pattern;
- [`AMReX_GMRES_MV.H`](../../Src/LinearSolvers/AMReX_GMRES_MV.H):
  right-preconditioner callback integration;
- [`AMReX_LUSolver.H`](../../Src/Base/AMReX_LUSolver.H):
  fixed-size pivoted dense LU.

Two prototype branches provide the starting designs:

- `weiqun/alg` supplies the `AMG<T>` shell, strength graph, influence-count
  concept, PMIS-like selection, V-cycle storage, and an algebra test;
- [`weiqun/spgemm`](https://github.com/WeiqunZhang/amrex/tree/spgemm)
  supplies a work-in-progress distributed `SpGEMM`, including a CUDA local
  product.

Neither branch is complete. In particular, the AMG branch has no interpolation
or Galerkin hierarchy, and most of the stronger SpGEMM tests are disabled.
Prototype commits should therefore be rebased and reviewed, not merged
wholesale.

## 5. Public Interface and Ownership

Keep the prototype's `AMG<T>` name and setup/cycle split, but make the
preconditioner contract explicit:

```cpp
template <typename T>
class AMG
{
public:
    struct Options {
        T strong_threshold = T(0.25);
        T max_row_sum = T(0.9);
        int max_interp_elements = 4;
        int max_levels = 25;
        int max_coarse_size = 9;
        int pre_sweeps = 1;
        int post_sweeps = 1;
        int priority_seed = 0;
    };

    explicit AMG(SpMatrix<T> const& A, Options options = {});

    void setup();

    // Apply one zero-initialized V-cycle: z = M^{-1} r.
    void apply(AlgVector<T>& z, AlgVector<T> const& r);

    // Convenience stationary-AMG solve using repeated V-cycle corrections.
    void solve(AlgVector<T>& x, AlgVector<T> const& b,
               T relative_tolerance, T absolute_tolerance,
               int maximum_iterations);
};
```

`AMG` references the fine matrix and owns every derived object: strength
graphs, C/F markers, interpolation/restriction operators, coarse matrices,
smoother denominators, work vectors, and the coarse LU factorization. The fine
matrix must outlive `AMG` and must not change after `setup()`.

`apply` always starts its output correction at zero and performs exactly one
V-cycle. It contains no convergence test. This makes it suitable for
`GMRES_MV::setPrecond` and keeps the preconditioner application fixed.

Do not add public strategy enums until a second implementation of one of these
stages exists.

## 6. Per-Level Data

Retain the prototype vectors and add only the state needed to complete a
hierarchy:

```text
AMGLevel
  A                 system matrix
  S, ST             strength graph and transpose
  cf_marker         undecided / fine / coarse
  coarse_id         local and off-rank global coarse IDs
  P, R              interpolation and transpose restriction
  l1_norm           sum_j |a_ij|
  rhs, correction   V-cycle vectors
  residual          b - A x
```

Level zero references the caller's matrix. Coarse levels own their matrices.
`P` has the fine row partition and coarse column partition; `R` has the coarse
row partition and fine column partition.

## 7. Hierarchy Setup

### 7.1 Validate and prepare the matrix

For each level:

1. Require a square matrix and a compatible row/column partition.
2. Require exactly one nonzero diagonal entry per row.
3. Sort and deduplicate CSR before setup if the producer did not guarantee
   those properties.
4. Prepare the reusable off-rank column communication pattern.
5. Cache the diagonal and row L1 norm.

The first version should reject unsupported structure with a clear message
rather than silently building a poor hierarchy.

### 7.2 Build the strength graph

For row \(i\), define

\[
q_{ij}=-\operatorname{sign}(a_{ii})a_{ij},\qquad
m_i=\max_{k\ne i}\max(q_{ik},0).
\]

Point \(i\) strongly depends on \(j\) when

\[
q_{ij}\geq \theta m_i,\qquad \theta=0.25.
\]

Before storing those edges, compute the diagonal-inclusive row sum. When

\[
\left|\sum_j a_{ij}\right| > 0.9\,|a_{ii}|,
\]

mark every off-diagonal entry in that row weak. Setting `max_row_sum = 1`
disables this default all-weak-row test.

Store only strong off-diagonal column indices. Do not retain zero-valued
entries as the prototype currently does. A compact graph makes PMIS loops
unambiguous and reduces setup traffic.

Use a GPU row kernel with one block or warp per sufficiently short row and a
portable fallback for long rows. The host and device implementations must use
the same inequality at the threshold.

### 7.3 Compute PMIS priorities

The integer influence measure is the column sum of the strength graph:

\[
\mu_i = \sum_k S_{ki}.
\]

Reuse the prototype construction
`ST = transpose(S)` followed by `ST.rowSum()`. Augment the integer measure with
a deterministic fractional tie-breaker derived from the global row ID:

\[
\pi_i=\mu_i+u(\operatorname{hash}(g_i,\mathrm{seed})),\qquad 0<u<1.
\]

The hash makes results reproducible across repeated runs and independent of
GPU scheduling. It need not reproduce HYPRE's random stream.

### 7.4 Perform PMIS C/F splitting

Maintain an active set of undecided points. In each round:

1. Exchange active states and priorities for off-rank strength neighbors.
2. Select active points whose priority is greater than every active conflict
   neighbor, where a directed strength edge in either direction creates a
   conflict. The selected points are independent within that PMIS round;
   later rounds can add a coarse point joined to an earlier coarse point in
   the directed strength graph.
3. Mark selected points coarse.
4. Mark an active point fine when it strongly depends on a newly selected
   coarse point.
5. Compact the remaining active points with an AMReX prefix scan.
6. Use an MPI sum to determine whether any undecided points remain globally.

Ties fall back to global row ID, although the fractional priority should make
ties exceptional. Promote isolated points to coarse points so every component
has a coarse representative and no interpolation row is accidentally empty.

PMIS is a C/F splitting algorithm. No aggregate label is created at any point.

### 7.5 Number coarse points

Count coarse points locally, all-gather the counts, and construct the coarse
`AlgPartition`. A device exclusive scan assigns local coarse IDs, and the MPI
prefix gives each rank's global coarse offset. Exchange global coarse IDs for
off-rank strong neighbors.

Do not encode coarse IDs in floating-point values. Add a small typed halo
exchange for `int`, `Long`, and matrix scalar fields by reusing the cached
SpMatrix send indices and neighbor lists.

### 7.6 Construct Extended+i interpolation

For a coarse row, insert the identity. For fine rows, extract the strong
fine-fine and fine-coarse submatrices. Fold diagonal and weak connections into
the Extended+i row scale, exchange the strong-coarse sums needed by remote
fine neighbors, and form the fine interpolation block with one distributed
sparse product. The companion LaTeX document gives the exact equations used
by BoomerAMG interpolation type 6.

After the product, retain at most four largest-magnitude entries in every row.
Use global coarse column ID as a deterministic tie-breaker, preserve CSR
column order, and rescale the retained entries so their sum equals the
untruncated row sum. If the retained sum cancels to zero, add the row-sum
correction to the first retained entry. Set `max_interp_elements = 0` to
disable this cap. The HYPRE comparison uses `PMaxElmts = 4` and
`TruncFactor = 0`.

If a fine point has no strong coarse neighbor, fail setup with row information.
That condition indicates a PMIS or strength-graph bug within the initial
supported matrix class.

### 7.7 Form restriction and the coarse operator

Build

\[
R=P^T,\qquad B=AP,\qquad A_c=RB.
\]

Use the existing distributed transpose and a repaired `SpGEMM`.

Before AMG depends on SpGEMM, its independent tests must cover:

- identity on the left and right;
- rectangular products;
- permutation matrices;
- \((AB)^T=B^TA^T\);
- a known Laplacian square;
- empty local row ownership;
- MPI off-rank rows;
- CPU and enabled GPU backends.

Every product must produce sorted, duplicate-free CSR and explicit row and
column partitions.

### 7.8 Stop and factor the coarse system

Continue setup until:

- the global row count is at most nine;
- `max_levels` is reached; or
- a level fails to reduce the number of unknowns.

The normal successful stop is at most nine rows. Gather the coarse matrix to a
dense \(9\times9\) array, pad inactive rows and columns with identity, and
factor it once with `LUSolver<9,T>`. At apply time, all-gather the tiny coarse
right-hand side, solve redundantly or on the I/O rank, and distribute the
active solution entries.

When PMIS produces no reduction, or `max_levels` is reached before the dense
threshold, terminate the hierarchy at the current level. A zero-initialized
V-cycle applies one L1-Jacobi sweep there instead of attempting a dense solve.
This matches BoomerAMG's terminal behavior for these setup conditions.

## 8. L1-Jacobi Smoother

Add a separate `L1JacobiSmoother` rather than changing the established
weighted-Jacobi behavior.

For every row, cache

\[
d_i=\sum_j |a_{ij}|.
\]

One sweep is

\[
x_i \leftarrow x_i+\frac{b_i-(Ax)_i}{d_i}.
\]

Use one pre-sweep and one post-sweep. If the initial correction is known to be
zero, the first sweep can use \(Ax=0\) without launching SpMV. This follows the
optimization already present in the prototype smoother.

## 9. V-Cycle and Solver

At level \(\ell\), starting from correction \(x_\ell\):

1. Apply one L1-Jacobi pre-sweep.
2. Compute \(r_\ell=b_\ell-A_\ell x_\ell\).
3. Restrict \(b_{\ell+1}=R_\ell r_\ell\).
4. Set \(x_{\ell+1}=0\) and recursively solve the error equation.
5. Correct \(x_\ell\leftarrow x_\ell+P_\ell x_{\ell+1}\).
6. Apply one L1-Jacobi post-sweep.

At the bottom, use the cached dense LU solve when the level has at most nine
rows; otherwise use the terminal zero-initialized L1-Jacobi sweep described
above.

`apply(z,r)` sets \(z=0\) and executes this cycle once. `solve` repeatedly
computes a true residual and adds one V-cycle correction:

\[
x^{k+1}=x^k+\mathcal V(A,b-Ax^k,0).
\]

The stationary solve is useful for direct comparison with BoomerAMG. The
primary production use remains a one-cycle right preconditioner for
`GMRES_MV`.

## 10. HYPRE Oracle

The comparison must explicitly configure HYPRE instead of relying on
AMReX's current `HypreIJIface` defaults, which select different coarsening,
interpolation, relaxation, sweep counts, and truncation.

Use:

```text
max_iterations       = 1 when used as a preconditioner
precond_tolerance     = 0
coarsen_type          = 8
interp_type           = 6
cycle_type            = 1
relax_order           = 0
down_relax_type       = 18
up_relax_type         = 18
coarse_relax_type     = 9
num_down_sweeps       = 1
num_up_sweeps         = 1
num_coarse_sweeps     = 1
strong_threshold      = 0.25
max_row_sum           = 0.9
max_levels            = 25
max_coarse_size       = 9
agg_num_levels        = 0
pmax_elmts            = 4
trunc_factor           = 0
keep_transpose         = 1
```

The benchmark should configure these setters directly in its HYPRE adapter so
a runtime-input omission cannot silently change the oracle.

## 11. Correctness Tests

### 11.1 Unit tests

Add focused tests for:

1. strength classification on fixed signed rows, including entries exactly at
   the threshold;
2. deterministic PMIS output for a fixed graph and seed;
3. PMIS invariants:
   - every point is C or F;
   - every selection round is independent in the current directed strength
     graph;
   - every F point strongly depends on a C interpolation neighbor;
4. coarse global numbering across one, two, and four MPI ranks;
5. Extended+i interpolation on prescribed C/F patterns;
6. identity rows for coarse points;
7. largest-magnitude interpolation truncation, deterministic ties, disabled
   truncation, and row-sum preservation;
8. maximum-row-sum all-weak classification and no-reduction terminal levels;
9. constant preservation, \(P\mathbf 1_c=\mathbf 1_f\), for zero-row-sum
   diffusion matrices;
10. exact transpose action, \(y^TRx=(Py)^Tx\);
11. Galerkin action,
   \(A_cv=R(A(Pv))\), without depending on CSR entry order;
12. one L1-Jacobi sweep against a hand-computed result;
13. the dense padded coarse solve for every active size from one through nine.

### 11.2 End-to-end matrices

Use manufactured, nonsingular matrices with an exact solution:

- two-dimensional shifted Poisson;
- three-dimensional shifted Poisson;
- variable-coefficient diffusion with moderate coefficient contrast;
- an anisotropic diffusion case after the isotropic cases pass.

Run with a zero initial guess on:

- one rank;
- two and four MPI ranks;
- the CPU;
- CUDA first, followed by HIP and SYCL when SpGEMM support exists.

Do not use a singular periodic Poisson matrix for the initial acceptance test.

### 11.3 Tolerance agreement

For zero initial guess, define

\[
\tau=\max\left(\mathrm{atol},
               \mathrm{rtol}\,\lVert b\rVert_2\right).
\]

After both solves, independently recompute the unpreconditioned true residual
with AMReX SpMV. Require

\[
\lVert b-Ax_{\mathrm{AMReX}}\rVert_2\leq\tau,\qquad
\lVert b-Ax_{\mathrm{HYPRE}}\rVert_2\leq\tau.
\]

On the conditioned manufactured benchmark, also require

\[
\lVert x_{\mathrm{AMReX}}-x_{\mathrm{HYPRE}}\rVert_2
\leq
\mathrm{atol}
+\mathrm{rtol}\,\lVert x_{\mathrm{HYPRE}}\rVert_2.
\]

When an exact solution is available, report both exact-solution errors as
additional diagnostics.

Do not require identical C/F markers, interpolation entries, level counts,
iteration counts, or bitwise solutions. PMIS tie-breaking and sparse reduction
order legitimately differ.

## 12. Performance Diagnostics

Correctness is the initial pass criterion. Record, but do not initially gate:

- setup and solve time;
- number of levels;
- coarse/fine ratio on each level;
- grid complexity
  \(\sum_\ell n_\ell/n_0\);
- operator complexity
  \(\sum_\ell \operatorname{nnz}(A_\ell)/
    \operatorname{nnz}(A_0)\);
- interpolation complexity;
- V-cycle count or outer GMRES iteration count;
- peak hierarchy storage.

The four-entry Extended+i cap controls interpolation and Galerkin fill. Record
both interpolation and operator complexity so any robustness/complexity
tradeoff remains visible.

## 13. Implementation Slices

### Slice 1: Production SpGEMM substrate

- Rebase the useful `weiqun/spgemm` changes onto current `development`.
- Make row and column partitions explicit for rectangular matrices.
- Repair CPU, CUDA, and MPI paths.
- Enable and pass all independent SpGEMM tests.
- Do not add AMG yet.

### Slice 2: Complete hierarchy setup on one rank

- Rebase `weiqun/alg`.
- Compact the strength graph.
- Complete deterministic PMIS.
- Add coarse numbering and Extended+i interpolation.
- Build \(R\) and \(P^TAP\).
- Add hierarchy invariant tests.

### Slice 3: Complete the V-cycle

- Add L1-Jacobi.
- Add recursive restriction/prolongation.
- Add padded `LUSolver<9,T>` coarse solve.
- Expose one-cycle `apply` and stationary `solve`.
- Pass serial manufactured-solution tests.

### Slice 4: MPI and GPU completion

- Add the typed ghost-field exchange.
- Make PMIS and coarse numbering globally correct.
- Complete GPU strength, PMIS, interpolation, and smoother kernels.
- Validate CPU, MPI, and CUDA results before adding other GPU backends.

### Slice 5: HYPRE parity and user documentation

- Add the exact HYPRE oracle configuration.
- Add tolerance-agreement tests and benchmark reporting.
- Document the new solver in
  `Docs/sphinx_documentation/source/LinearSolvers.rst`.
- Record exact build/test invocations and results in the eventual PR.

## 14. Baseline Validation Commands

Use the repository CMake test workflow:

```bash
cmake -S . -B build-amg \
  -DAMReX_ENABLE_TESTS=ON \
  -DAMReX_TEST_TYPE=Small \
  -DAMReX_LINEAR_SOLVERS=ON \
  -DAMReX_HYPRE=ON
cmake --build build-amg -j8
ctest --test-dir build-amg -R 'SpGEMM|AMG' --output-on-failure
```

Separate configurations are required for each enabled GPU backend. MPI tests
must be registered with explicit process counts so `ctest` exercises both
local and off-rank matrix paths.

## 15. Definition of Done

The initial solver is complete when:

1. the SpGEMM prerequisite passes serial, MPI, and target-GPU tests;
2. all hierarchy invariants pass;
3. the V-cycle has no placeholder restriction, interpolation, or coarse solve;
4. `apply` is a fixed one-cycle preconditioner;
5. scalar shifted-Poisson and variable-diffusion tests pass on supported
   backends;
6. AMReX and explicitly configured HYPRE satisfy the true-residual and solution
   agreement criteria;
7. operator/grid complexity and timings are reported;
8. user-facing linear-solver documentation is updated; and
9. the exact validation commands and results are captured in the PR.

The next milestone is measured GPU parity: run the matching HYPRE oracle and
native solver on CUDA, followed by HIP and SYCL, and compare hierarchy
complexity, setup time, and V-cycle throughput.

## 16. Implementation Status

The CPU-validated solver and raw GPU-default parity changes described by this
plan were completed on 2026-07-31:

- rectangular sparse matrices retain explicit row and column partitions;
- distributed SpGEMM imports only referenced remote right-operand rows and
  emits sorted, duplicate-free CSR;
- AMG setup uses compact strength graphs with the maximum-row-sum rule,
  deterministic PMIS, typed sparse field exchange, matrix-based Extended+i
  interpolation capped at four entries, transpose restriction, and Galerkin
  coarse operators;
- the V(1,1)-cycle uses a separate L1-Jacobi smoother, a cached padded dense LU
  coarse solve, and a terminal L1-Jacobi sweep when setup stops above nine
  rows;
- hierarchy complexity, storage, setup, apply, and solve diagnostics are
  exposed through the public API;
- focused algebra tests, four manufactured systems, the HYPRE oracle, and
  explicit one-, two-, and four-rank CTest registrations are in the Small
  suite.

Validation was repeated on 2026-07-31 after the GPU-default parity changes:

```text
Serial Small suite:
  ctest --test-dir /private/tmp/amrex-amg-small-audit \
        -R 'Algebra_(AMG|SpGEMM)' --output-on-failure
  2/2 passed

MPI without HYPRE:
  ctest --test-dir /private/tmp/amrex-amg-audit \
        -R 'Algebra_(AMG|SpGEMM)' --output-on-failure
  6/6 passed at the registered one-, two-, and four-rank counts

MPI with HYPRE 0beb8b47a:
  ctest --test-dir /private/tmp/amrex-amg-phase1-hypre \
        -R 'Algebra_(AMG|SpGEMM)' --output-on-failure
  6/6 passed, including residual and solution agreement

Debug AddressSanitizer and UndefinedBehaviorSanitizer:
  ASAN_OPTIONS=detect_leaks=0 \
  ctest --test-dir /private/tmp/amrex-amg-sanitize-audit \
        -R 'Algebra_(AMG|SpGEMM)' --output-on-failure
  2/2 passed
```

The Sphinx sources parse successfully; the full dummy build reports only the
pre-existing missing `source/amrex.pdf` download warning. The companion
equations PDF was regenerated with `latexmk` and passes `qpdf --check`. CUDA,
HIP, and SYCL execution remain unverified because this host has none of their
compilers, and every existing AMReX build is configured with
`AMReX_GPU_BACKEND=NONE`.

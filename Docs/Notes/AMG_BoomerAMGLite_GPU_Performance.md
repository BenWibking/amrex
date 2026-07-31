# GPU Execution and Performance of the Minimal BoomerAMG-Like Solver

This note describes how the operations in
[AMG_BoomerAMGLite_Equations.tex](AMG_BoomerAMGLite_Equations.tex) map to GPU
kernels and MPI communication. It also identifies the operations most likely
to limit setup time, V-cycle throughput, memory use, and strong scaling.

The implementation plan is
[AMG_BoomerAMGLite_Plan.md](AMG_BoomerAMGLite_Plan.md).

## 1. Scope

The solver profile considered here is:

- classical sign-based strength of connection;
- the `max_row_sum = 0.9` all-weak-row rule;
- PMIS C/F splitting;
- matrix-based Extended+i interpolation capped at four entries per row;
- transpose restriction;
- Galerkin coarse operators;
- one L1-Jacobi pre-sweep and post-sweep;
- a multiplicative V-cycle;
- a dense coarse solve for at most nine global unknowns.

This is the raw BoomerAMG GPU algorithmic profile. PMIS, Extended+i
interpolation, the four-entry interpolation cap, and L1-Jacobi are all
GPU-supported BoomerAMG defaults, but matching those choices does not imply
that a new AMReX implementation will initially match HYPRE performance.

The correctness gate remains agreement with HYPRE to the requested input
tolerance. Performance measurements are meaningful only after that gate
passes.

## 2. Performance Model

AMG has two distinct performance phases:

1. **Setup** constructs the hierarchy. It uses graph construction, repeated
   PMIS rounds, sparse transpose, Extended+i interpolation construction, three
   sparse matrix-matrix products per level, truncation, and a coarse
   factorization.
2. **Apply** executes one fixed V-cycle. It uses sparse matrix-vector products,
   vector kernels, halo exchanges, and one coarse solve.

Setup and apply must be timed separately. Setup is irregular and allocation
heavy; apply is regular and should reuse all communication patterns,
descriptors, buffers, and work vectors.

The total work in a V-cycle is controlled by the hierarchy as well as by
individual kernel speed. In particular, operator complexity

\[
C_A =
\frac{\sum_{\ell=0}^{L}\operatorname{nnz}(A_\ell)}
     {\operatorname{nnz}(A_0)}
\]

sets the approximate amount of matrix data read by smoothing and residual
operations. A fast kernel cannot compensate for an unnecessarily dense
hierarchy. Grid, operator, and interpolation complexity must therefore be
reported beside timing results.

## 3. Kernel and Communication Map

| Mathematical step | GPU work | MPI or host synchronization | Expected difficulty |
|---|---|---|---|
| Matrix preparation | Validate rows, extract the diagonal, compute row L1 sums, and optionally sort or compact CSR | Global failure reduction only | Low to medium |
| Strength graph | Row maximum and row-sum/count kernel, exclusive scan, compact fill kernel | None for locally stored matrix entries | Medium |
| PMIS priorities | Structural transpose or sparse transpose, row sum, deterministic hash kernel | Distributed transpose communication | Medium |
| PMIS splitting | Candidate, fine-marking, state-update, and undecided-count kernels for every round | State/candidate halo exchanges and a termination all-reduce per round | High |
| Coarse numbering | C-point indicator, exclusive scan, global-ID kernel | Rank prefix/allgather and coarse-ID halo exchange | Low |
| Extended+i interpolation | Strong (A_{FF})/(A_{FC}) extraction, row reductions, sparse transpose, one SpGEMM,Docs/Notes/AMG_BoomerAMGLite_GPU_Performance.md and largest-magnitude row truncation | Marker, coarse-ID, and strong-coarse-sum halo exchanges plus remote sparse rows | High |
| Restriction | Sparse transpose of \(P\) | Distributed transpose communication | Medium |
| Galerkin product | Symbolic and numeric SpGEMM for \(B=AP\) and \(A_c=RB\) | Exchange of variable-length remote sparse rows and partial products | Very high |
| L1-Jacobi setup | Absolute row-sum reduction | None | Low |
| L1-Jacobi apply | SpMV followed by a pointwise correction; no SpMV for the first zero-guess sweep | Ordinary SpMV halo exchange | Low kernel complexity |
| V-cycle | Four SpMVs and several vector kernels per non-coarse level | Level-by-level halo exchanges | Medium to high |
| Dense coarse solve | One thread or warp performs triangular solves | Gather, all-gather, or broadcast of the coarse problem and correction | High at large MPI counts |
| Residual and norms | SpMV, vector transform, and sum-of-squares reduction | One all-reduce per reported global norm | Low |
| Complexity metrics | Read matrix metadata and sum counts | Small all-reductions | Negligible |

## 4. Setup Operations

### 4.1 Matrix preparation

The matrix is validated once before hierarchy construction. One row-parallel
kernel can:

- locate and cache \(a_{ii}\);
- count diagonal entries;
- check that the diagonal is nonzero;
- compute \(\sum_j |a_{ij}|\);
- record unsupported sign or structure conditions.

A device reduction combines row failure flags. MPI then combines the local
result into a global setup decision.

The existing
[`SpMatrix::diagonalVector`](../../Src/LinearSolvers/AMReX_SpMatrix.H) is
already a row-parallel device operation. The existing
[`CSR::sort`](../../Src/LinearSolvers/AMReX_CSR.H) uses warp sorting for short
rows and segmented radix sorting for longer rows on CUDA and HIP. Sorting
should occur before the matrix is split into local and remote column blocks.

Sorting alone does not combine duplicate columns. If input producers may emit
duplicates, setup also needs a segmented reduction or must require canonical
CSR as a precondition.

### 4.2 Strength graph

Strength construction should build compact CSR in two passes.

The first row kernel computes

\[
m_i = \max_{j\ne i}\max(-\operatorname{sign}(a_{ii})a_{ij},0)
\]

and counts entries satisfying the strength test. A device exclusive scan of
the counts produces the strength-graph row offsets. The count and fill kernels
also compute the diagonal-inclusive row sum and emit no edges when its
magnitude exceeds `max_row_sum * abs(diagonal)`. A second row kernel writes
only the selected column indices.

The matrix values for both locally owned and off-rank columns are already
owned by the row's rank. No halo exchange is required to test their strength.
The kernel must, however, traverse both pieces of the split `ParCsr` row.

A single fixed row mapping will not perform well for every level:

- a thread per row is appropriate for very short stencil rows;
- a warp per row is appropriate for medium rows;
- a block per row is appropriate for unusually long coarse rows.

The initial implementation can use one warp per row, but production tuning
should bucket rows by length. Fine-grid 5-, 7-, or 9-point rows otherwise
waste most warp lanes, while coarse rows can exceed a warp's convenient
working set.

### 4.3 Structural transpose and PMIS priorities

PMIS needs both \(S\) and \(S^T\). The influence count

\[
\mu_i = (S^T\mathbf{1})_i
\]

is then one row-reduction kernel, and the deterministic fractional priority is
one pointwise hash kernel.

Weiqun's prototype stores the graph as `SpMatrix<short>`. The current CUDA and
HIP sparse-transpose dispatch supports floating and complex matrix values, not
`short`. There are two implementation choices:

1. Store numerical ones using `SpMatrix<Real>` and immediately reuse the
   existing distributed transpose.
2. Add an indices-only `CSRGraph` and a structural transpose.

The first choice minimizes initial code. The second avoids storing a value for
every edge in both \(S\) and \(S^T\), and is the preferable production design.
A structural transpose consists of column counts, an exclusive scan, a
scatter, and optionally a segmented sort.

The existing distributed transpose stages off-rank data through communication
buffers. That is acceptable for a one-time setup operation, but a graph
transpose should eventually avoid numerical value traffic.

### 4.4 PMIS rounds

PMIS is the most synchronization-sensitive graph operation. A race-free round
can be implemented with pull kernels:

1. Exchange current state values needed for remote conflict neighbors.
2. Select candidates by scanning \(S_i\cup S_i^T\) and comparing priorities.
3. Exchange candidate flags needed by remote strength rows.
4. Mark a point fine when its \(S_i\) row contains a selected coarse point.
5. Update state and locally reduce the number of undecided points.
6. All-reduce the undecided count to determine global termination.

Pull kernels avoid multiple selected points racing to write a neighbor's
state. They also make the result deterministic for a fixed priority seed.

The priority is static and needs to be exchanged only once. State and
candidate flags change each round. The expected bottleneck is therefore not
the comparison kernel but the sequence of small launches, halo exchanges, and
global termination reductions.

The host cannot directly iterate device arrays as in the prototype sketch.
Each PMIS phase must be an explicit device kernel, and MPI builds need a
device-compatible halo plan.

### 4.5 Generic graph halo exchange

The current `SpMatrix` SpMV exchange is tied to the matrix scalar type.
Hierarchy setup instead needs to exchange several payload types over the same
remote-column adjacency:

- floating-point priorities;
- compact state and candidate markers;
- global `Long` coarse IDs.

The reusable abstraction should therefore be a communication plan containing
neighbor lists, counts, and device packing indices, with a payload-typed
`exchange<T>()` operation. The adjacency discovery is performed once; only
payload buffers change between exchanges.

Without this separation, PMIS and interpolation will duplicate SpMV
communication logic or encode integer data in floating-point vectors.

### 4.6 Distributed coarse numbering

Numbering is a standard scan operation:

1. Convert each local marker to a C-point indicator.
2. Perform a device exclusive scan.
3. Obtain the local C-point count from the scan result.
4. Use an MPI exclusive prefix or gathered count array to obtain the rank
   offset.
5. Add that offset in a pointwise kernel.
6. Exchange global coarse IDs required by off-rank interpolation rows.

This stage should not require specialized optimization. Its communication
volume is one count per rank plus one integer per remote coarse point.

### 4.7 Matrix-based Extended+i interpolation

Extended+i first extracts strong fine-fine and fine-coarse CSR matrices.
Row-parallel kernels compute the diagonal-plus-weak sum and the strong-coarse
sum, while the typed halo supplies strong-coarse sums for remote fine
neighbors. A distributed transpose of the fine-fine matrix provides reverse
couplings used by the Extended+i scaling. One SpGEMM then forms the raw
interpolation product.

A final truncation pass ranks each row by decreasing weight magnitude, keeps
at most four entries, scans the retained counts, and rescales the retained
weights to preserve the untruncated row sum. Global coarse column IDs break
magnitude ties deterministically. Performance concerns are:

- repeated scans of irregular strong rows and the fine-fine transpose;
- communication of strong-coarse sums and remote rows for the product;
- symbolic and numeric SpGEMM overhead on small coarse levels;
- ranking and compaction for the four-entry cap;
- sorting and splitting the resulting rectangular matrix.

This adds one sparse product per hierarchy level before the two Galerkin
products. It can therefore be a material part of setup time even when the cap
successfully limits subsequent operator fill.

### 4.8 Restriction

Restriction is formed once as \(R=P^T\). Because \(P\) has floating-point
values, the existing
[`transpose`](../../Src/LinearSolvers/AMReX_SpMatUtil.H) supplies a usable
CUDA, HIP, SYCL, and MPI starting point.

The transpose path creates sparse-library handles and temporary buffers and
uses host staging for parts of MPI transpose setup. Those costs are secondary
because restriction is constructed only once per hierarchy level. They should
not be copied into the repeated V-cycle path.

### 4.9 Galerkin coarse operators

The coarse operator is built as

\[
B_\ell = A_\ell P_\ell,
\qquad
A_{\ell+1} = R_\ell B_\ell.
\]

This is expected to dominate setup time and temporary memory. For a
distributed product \(C=AB\), the local rows of \(A\) can reference rows of
\(B\) owned by other ranks. The implementation must:

1. discover the required remote \(B\) rows;
2. exchange row lengths, column indices, and values;
3. execute symbolic products to determine output sparsity;
4. allocate output CSR and temporary accumulators;
5. execute numeric products;
6. merge local and remote partial products;
7. combine duplicate columns;
8. sort and split the result for subsequent operations.

Sparse row lengths and product sizes are data dependent. Symbolic work,
temporary allocation, hash or merge strategy, and output fill all change from
one level to the next.

Weiqun's `spgemm` branch is a useful design sketch, but it is not a production
substrate:

- only the local CUDA path delegates to cuSPARSE SpGEMM;
- the CUDA path narrows CSR indices to 32 bits;
- distributed product assembly is unfinished;
- there is no equivalent complete HIP or SYCL product path;
- most of the stronger algebraic tests are disabled.

The first implementation should keep the clear two-product formulation. A
fused RAP kernel could avoid materializing \(B_\ell\), but it would combine
distributed row import, two levels of symbolic expansion, and numerical
accumulation in one substantially harder operation.

SpGEMM should be implemented and tested as a standalone algebra primitive
before AMG depends on it.

### 4.10 L1 denominators

The cached denominator

\[
d_{\ell,i} = \sum_j |(A_\ell)_{ij}|
\]

is one row-reduction kernel per level. It traverses the same local and remote
CSR pieces as `rowSum`. This cost is small relative to RAP and is paid only
during setup.

### 4.11 Coarse matrix preparation

When \(n_L\leq9\), the dense matrix is gathered or replicated and factored
once. The local arithmetic is negligible. The setup design must instead
define:

- which ranks own the dense matrix;
- how CSR entries are gathered and duplicate contributions are combined;
- whether all ranks store the LU factors or only a designated root does;
- how ranks with zero coarse rows participate.

Replicating a \(9\times9\) factorization is inexpensive and avoids a broadcast
of the solution after every solve, but the coarse right-hand side still
requires a collective assembly.

## 5. V-Cycle Execution

For a zero-initialized V(1,1) preconditioner application, every non-coarse
level performs approximately:

```text
x  = D_L1^-1 b              pointwise kernel
r  = b - A x                A SpMV + vector update
bc = R r                    R SpMV
ec = Vcycle(bc)             recursive level
x += P ec                   P SpMV + vector update
x += D_L1^-1 (b - A x)      A SpMV + pointwise update
```

Thus each non-coarse level uses four SpMVs:

1. one fine residual after pre-smoothing;
2. one restriction;
3. one prolongation;
4. one matrix application for post-smoothing.

The level recursion imposes a strict dependency chain. Work on level
\(\ell+1\) cannot start until restriction on level \(\ell\) finishes, and
post-smoothing cannot start until the coarse correction returns. Concurrency
across levels is therefore limited.

Fine levels are bandwidth bound. As the hierarchy shrinks, kernel launch,
sparse-library setup, and MPI latency quickly dominate useful arithmetic.

## 6. Repeated SpMV Costs

The current
[`SpMV`](../../Src/LinearSolvers/AMReX_SpMV.H) GPU path creates and destroys
backend handles and sparse descriptors, allocates workspace, and synchronizes
the stream for every multiplication. The distributed wrapper caches its
communication pattern, but send and receive payload buffers and requests are
created for each application.

That behavior is tolerable for occasional algebra operations but is too
expensive for four SpMVs per level per preconditioner application.

Each hierarchy matrix should own or reference a reusable `SpMVPlan` containing:

- the backend sparse-matrix descriptor;
- reusable dense-vector descriptors or cheap pointer-update support;
- reusable backend workspace;
- persistent packing indices and communication counts;
- reusable send and receive buffers;
- any backend analysis result;
- a row-kernel fallback decision for small matrices.

The local matrix product should overlap remote communication:

1. post receives;
2. pack and send requested vector entries;
3. compute the local-column CSR product;
4. wait for receives;
5. accumulate the remote-column CSR product.

The current split CSR representation already supports this basic schedule.
Avoiding unconditional stream synchronization between these phases is
essential.

For sufficiently small coarse matrices, a portable AMReX row kernel will
usually be cheaper than creating or invoking a general sparse-library
operation. The crossover should be measured separately for CUDA, HIP, and
SYCL rather than fixed from one backend.

## 7. L1-Jacobi Application

The existing
[`JacobiSmoother`](../../Src/LinearSolvers/AMReX_Smoother_MV.H) provides the
control-flow pattern, but it uses a weighted matrix diagonal rather than the
L1 denominator.

For the first pre-sweep in a zero-initialized V-cycle,

\[
x_i = b_i/d_i
\]

is a single pointwise kernel. No SpMV is required.

The post-sweep requires \(A x\). A correctness-first implementation can call
SpMV and then launch

\[
x_i \mathrel{+}= \frac{b_i-(Ax)_i}{d_i}.
\]

After the baseline is working, the remote-accumulation completion and
pointwise smoother update can potentially be fused. That optimization is
backend- and communication-sensitive and should not complicate the initial
implementation.

## 8. Coarse-Level and Multi-GPU Scaling

The \(9\times9\) LU arithmetic is not a GPU optimization problem.
[`LUSolver<9,T>`](../../Src/Base/AMReX_LUSolver.H) is already callable on host
or device. A single thread can solve the padded system.

The performance problem is reducing a distributed hierarchy to nine global
unknowns. With many MPI ranks:

- most ranks eventually own no rows;
- halo messages become very small;
- collective latency dominates;
- a one-thread GPU launch performs almost no useful work.

The first implementation should use a replicated coarse matrix and
factorization, with an explicit collective to assemble the coarse right-hand
side. A later process-coarsening design can transfer coarse levels to a
smaller communicator. Optimizing the triangular-solve arithmetic itself is
not worthwhile.

## 9. Relative Optimization Difficulty

The implementation and performance risks, in descending order, are:

1. **Distributed SpGEMM and RAP.** Dynamic sparsity, variable-length sparse-row
   communication, partial-product merging, and operator fill make this the
   largest setup problem.
2. **Multi-GPU PMIS.** The work per round is small, but each round requires
   irregular neighborhood reads, changing halo data, and global termination.
3. **SpMV lifecycle and coarse-level latency.** Descriptor creation,
   allocation, synchronization, and tiny sparse operations repeat throughout
   every V-cycle.
4. **Coarse-rank ownership.** The dense solve is trivial; communicator and
   data-movement policy determine scalability.
5. **Structural graph transpose and typed halo exchange.** These are contained
   infrastructure tasks but are prerequisites for an efficient PMIS path.
6. **Extended+i construction and truncation.** It adds extraction, transpose,
   a distributed sparse product, row ranking, and compaction before RAP.
7. **Strength, L1 sums, numbering, vector updates, and norms.** These are
   standard row, scan, pointwise, or reduction kernels.

The deterministic hash used for PMIS priorities and the dense LU arithmetic
should not receive early optimization effort.

## 10. Recommended Optimization Sequence

### Phase 1: Correct device execution

1. Implement compact strength CSR with portable row kernels.
2. Implement a correctness-first graph transpose.
3. Implement deterministic PMIS with explicit device kernels.
4. Build matrix-based Extended+i interpolation and its four-entry,
   row-sum-preserving truncation.
5. Implement and validate distributed SpGEMM independently.
6. Complete the V-cycle using existing SpMV and vector operations.
7. Pass the HYPRE residual and solution-agreement tests.

### Phase 2: Remove repeated overhead

1. Cache sparse-library handles, descriptors, analyses, and workspaces.
2. Retain communication payload buffers across V-cycles.
3. Avoid host synchronizations between dependent operations on one stream.
4. Add a custom row-kernel SpMV path for small levels.
5. Reuse all level vectors rather than allocating during `apply`.

### Phase 3: Improve communication and scaling

1. Make the halo plan independent of payload type.
2. Use GPU-aware MPI buffers when supported by the AMReX configuration.
3. Overlap local SpMV work with halo exchange.
4. Reduce PMIS control messages and avoid redundant state exchanges.
5. Introduce process coarsening if the replicated coarse solve becomes a
   scalability limit.

### Phase 4: Tune sparse setup

1. Select vendor or native SpGEMM per backend and matrix regime.
2. Reuse symbolic products when the same sparsity pattern permits it.
3. Reduce temporary allocations and peak RAP memory.
4. Bucket row kernels by row length.
5. Tune the interpolation truncation ranking and compaction kernels without
   changing the four-entry, row-sum-preserving policy.

Fusing the Extended+i or Galerkin products belongs after these phases.

## 11. Measurement Plan

Record setup and apply measurements independently.

### Setup metrics

- total setup time;
- time for strength and graph transpose;
- PMIS round count and time;
- time for interpolation construction;
- time for \(P^T\);
- symbolic and numeric time for each SpGEMM;
- remote sparse-row bytes exchanged;
- peak temporary memory;
- grid, operator, and interpolation complexity.

### Apply metrics

- time per V-cycle;
- time per level;
- SpMV time for \(A\), \(R\), and \(P\);
- vector-kernel time;
- MPI wait time and communicated bytes;
- coarse-solve collective time;
- number of kernel launches and stream synchronizations.

### Test regimes

At minimum, measure:

1. one MPI rank and one GPU;
2. multiple ranks sharing one node;
3. one rank per GPU across multiple nodes;
4. fixed local problem size for weak scaling;
5. fixed global problem size for strong scaling;
6. hierarchy reuse across many preconditioner applications.

Use the same matrix and explicit BoomerAMG profile for comparative runs.
Report correctness, setup time, apply time, hierarchy complexity, and memory
together. A faster V-cycle with substantially worse convergence or operator
complexity is not an equivalent result.

## 12. Performance-Ready Definition

The implementation is ready for performance comparison when:

1. all setup and apply operations remain on the device except explicitly
   documented MPI staging and the optional coarse solve;
2. PMIS contains no host loops over device-resident arrays;
3. `apply()` performs no dynamic hierarchy or work-vector allocation;
4. sparse descriptors and workspaces are reused;
5. MPI communication patterns are built during setup, not during each cycle;
6. setup and V-cycle profiles contain no unexplained device-wide
   synchronization;
7. each level reports row counts, nonzeros, and communication volume;
8. both the AMReX solver and HYPRE meet the requested true-residual tolerance;
9. their solutions agree to the input tolerance on the manufactured
   benchmark;
10. timing comparisons use the matching
    PMIS/Extended+i/PMax-4/L1-Jacobi configuration.

The relevant HYPRE option and GPU-support reference is the
[BoomerAMG documentation](https://hypre.readthedocs.io/en/latest/solvers-boomeramg.html).

## 13. Current Validation Status

The raw GPU-default algorithmic profile is implemented and passes the serial,
MPI, HYPRE-oracle, and sanitizer algebra tests as of 2026-07-31. Actual GPU
execution and timing remain outstanding: this validation host has no CUDA,
HIP, or SYCL compiler, and all available AMReX build trees use
`AMReX_GPU_BACKEND=NONE`. The next performance run should therefore start with
the one-rank/one-GPU regime above and record the native and HYPRE hierarchy
complexities before interpreting setup or V-cycle timing.

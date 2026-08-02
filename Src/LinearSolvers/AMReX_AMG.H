#ifndef AMREX_AMG_H_
#define AMREX_AMG_H_
#include <AMReX_Config.H>

#include <AMReX_AlgHaloExchange.H>
#include <AMReX_AlgVecUtil.H>
#include <AMReX_HypreChebyshevSmoother.H>
#include <AMReX_L1JacobiSmoother.H>
#include <AMReX_LUSolver.H>
#include <AMReX_Math.H>
#include <AMReX_ParallelContext.H>
#include <AMReX_ParallelReduce.H>
#include <AMReX_Print.H>
#include <AMReX_Reduce.H>
#include <AMReX_Scan.H>
#include <AMReX_SpGEMM.H>
#include <AMReX_SpMatUtil.H>
#include <AMReX_SpMV.H>
#include <AMReX_Utility.H>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace amrex {

template <typename T>
struct AMGTestAccess;

/**
 * \file AMReX_AMG.H
 *
 * A deliberately narrow classical algebraic multigrid implementation for
 * scalar elliptic matrices. It uses classical strength, deterministic PMIS
 * C/F splitting, matrix-based Extended+i interpolation, Galerkin coarsening,
 * selectable L1-Jacobi or HYPRE-equivalent Chebyshev smoothing, and either a
 * dense solve on a last level of at most nine points or terminal relaxation
 * when coarsening stops early.
 */
/**
 * \brief Classical algebraic multigrid preconditioner for scalar elliptic
 * matrices.
 *
 * The hierarchy uses sign-based strength of connection, deterministic PMIS
 * C/F splitting, matrix-based Extended+i interpolation, transpose
 * restriction, Galerkin coarse operators, selectable L1-Jacobi or Chebyshev
 * smoothing, and a dense coarse solve when the terminal level has at most
 * nine points.
 *
 * \tparam T Floating-point matrix and vector scalar type.
 */
template <typename T>
class AMG
{
public:
    static_assert(std::is_floating_point_v<T>);

    /** \brief Native smoother choices and their HYPRE relaxation mappings. */
    enum class Smoother : int {
        chebyshev = 16, //!< HYPRE relaxation type 16.
        l1_jacobi = 18  //!< HYPRE relaxation type 18.
    };

    /**
     * \brief Setup and cycle controls for AMG.
     */
    struct Options {
        //! Classical strength-of-connection threshold in the interval (0,1].
        T strong_threshold = T(0.25);
        //! Row-sum threshold above which every off-diagonal entry is weak.
        T max_row_sum = T(0.9);
        //! Maximum interpolation entries per row; zero disables the cap.
        int max_interp_elements = 4;
        //! Maximum number of hierarchy levels, including the finest level.
        int max_levels = 25;
        //! Largest global level factored by the dense coarse solver.
        int max_coarse_size = 9;
        //! Smoother applied on every non-dense level.
        Smoother smoother = Smoother::chebyshev;
        //! Number of smoother applications before coarse-grid correction.
        int pre_sweeps = 1;
        //! Number of smoother applications after coarse-grid correction.
        int post_sweeps = 1;
        //! HYPRE Chebyshev residual-polynomial order in [1,4].
        int chebyshev_order = 4;
        /**
         * \brief HYPRE-style level-spectrum estimator control.
         *
         * Zero selects the algorithmically partition-independent Gershgorin
         * estimate. A positive value selects that many CG/Lanczos steps.
         */
        int chebyshev_eigenvalue_iterations = 10;
        //! HYPRE fraction of the estimated spectrum targeted by smoothing.
        T chebyshev_fraction = T(0.3);
        //! HYPRE Chebyshev polynomial variant (zero is standard).
        int chebyshev_variant = 0;
        //! Use HYPRE's symmetric diagonal scaling.
        bool chebyshev_scale = true;
        //! Seed used by the deterministic PMIS priority hash.
        int priority_seed = 0;
    };

    //! Persistent hierarchy information for one level.
    struct LevelDiagnostics {
        //! Global number of rows on this level.
        Long rows = 0;
        //! Global number of nonzeros in the level operator.
        Long nonzeros = 0;
        //! Global number of interpolation nonzeros below this level.
        Long interpolation_nonzeros = 0;
        //! Ratio of next-level rows to this level's rows.
        double coarse_fraction = 0.0;
    };

    //! Setup, hierarchy-complexity, storage, and recent solve diagnostics.
    struct Diagnostics {
        Vector<LevelDiagnostics> levels;
        double grid_complexity = 0.0;
        double operator_complexity = 0.0;
        double interpolation_complexity = 0.0;
        double setup_seconds = 0.0;
        double last_apply_seconds = 0.0;
        double last_solve_seconds = 0.0;
        Long hierarchy_storage_bytes = 0;
    };

    /**
     * \brief Bind an AMG hierarchy to the fine-grid matrix \p A.
     *
     * The matrix must outlive the AMG object and must not be modified after
     * setup.
     *
     * \param A Fine-grid sparse matrix.
     * \param options Hierarchy and cycle controls.
     */
    explicit AMG (SpMatrix<T> const& A, Options options = {})
        : m_fine_matrix(A), m_options(options)
    {}

    /**
     * \brief Construct and factor the complete multigrid hierarchy.
     */
    void setup ();

    /**
     * \brief Apply one zero-initialized V(1,1)-cycle,
     * \f$z=M^{-1}r\f$.
     *
     * \param z Destination for the preconditioned residual.
     * \param r Residual to which the fixed AMG preconditioner is applied.
     */
    void apply (AlgVector<T>& z, AlgVector<T> const& r);

    /**
     * \brief Solve by stationary defect correction with true-residual
     * stopping.
     *
     * Iteration stops when the residual norm is no greater than
     * `max(absolute_tolerance, relative_tolerance * initial_residual_norm)`,
     * or when \p maximum_iterations cycles have completed.
     *
     * \param x Initial guess on input and approximate solution on output.
     * \param b Right-hand side.
     * \param relative_tolerance Relative true-residual tolerance.
     * \param absolute_tolerance Absolute true-residual tolerance.
     * \param maximum_iterations Maximum number of V-cycles.
     */
    void solve (AlgVector<T>& x, AlgVector<T> const& b,
                T relative_tolerance, T absolute_tolerance,
                int maximum_iterations);

    //! Return the number of levels in the constructed hierarchy.
    [[nodiscard]] int numLevels () const noexcept
    {
        return static_cast<int>(m_levels.size());
    }

    //! Return the number of cycles performed by the most recent solve.
    [[nodiscard]] int lastIterations () const noexcept
    {
        return m_last_iterations;
    }

    //! Return the final true-residual norm from the most recent solve.
    [[nodiscard]] T lastResidualNorm () const noexcept
    {
        return m_last_residual_norm;
    }

    //! Return hierarchy, storage, and timing diagnostics.
    [[nodiscard]] Diagnostics const& diagnostics () const noexcept
    {
        return m_diagnostics;
    }

    //! Print hierarchy, complexity, storage, and timing diagnostics.
    void printDiagnostics () const;

private:
    template <typename U>
    friend struct AMGTestAccess;

public: // NOLINT  Private implementation details, but public for CUDA

    using matrix_type = SpMatrix<T>;
    using csr_type = typename matrix_type::csr_type;
    using helper_type = SpGEMMHelper<T,DefaultAllocator>;
    using host_csr_type = typename helper_type::host_csr_type;

    static constexpr int undecided = -1;
    static constexpr int fine = 0;
    static constexpr int coarse = 1;
    static constexpr int dense_coarse_size = 9;

    /**
     * \brief Matrices, graph data, vectors, and partitions owned by one
     * hierarchy level.
     */
    struct Level {
        //! Active matrix on this level.
        matrix_type const* A = nullptr;
        //! Storage for a coarse matrix; empty on the finest level.
        std::unique_ptr<matrix_type> owned_A;

        // Canonical local rows with global column IDs. These are retained
        // because SpMatrix may later split its CSR for communication.
        //! Canonical matrix rows with global column indices.
        csr_type rows;
        //! Compact outgoing strength graph.
        csr_type strength_rows;
        //! Compact incoming strength graph.
        csr_type strength_transpose_rows;
        //! Local-or-ghost slots corresponding to outgoing graph columns.
        Gpu::DeviceVector<Long> strength_col_slots;
        //! Local-or-ghost slots corresponding to incoming graph columns.
        Gpu::DeviceVector<Long> strength_transpose_col_slots;

        //! Distributed numerical representation of the strength graph.
        std::unique_ptr<matrix_type> S;
        //! Distributed transpose of the strength graph.
        std::unique_ptr<matrix_type> ST;
        //! Interpolation operator from the next coarser level.
        std::unique_ptr<matrix_type> P;
        //! Restriction operator formed as the transpose of interpolation.
        std::unique_ptr<matrix_type> R;

        //! Local PMIS coarse/fine marker for every owned point.
        Gpu::DeviceVector<int> cf_marker;
        //! Global coarse ID for owned coarse points, or -1 for fine points.
        Gpu::DeviceVector<Long> coarse_id;
        //! Distribution of points on the next coarser level.
        AlgPartition coarse_partition;
        //! Typed sparse exchange plan shared by setup graph fields.
        std::unique_ptr<AlgHaloExchange> halo;

        //! L1-Jacobi smoother and cached absolute row sums, when selected.
        std::unique_ptr<L1JacobiSmoother<T>> l1_jacobi_smoother;
        //! HYPRE-equivalent Chebyshev smoother, when selected.
        std::unique_ptr<HypreChebyshevSmoother<T>> chebyshev_smoother;
        //! Level right-hand-side workspace.
        AlgVector<T> rhs;
        //! Level correction workspace.
        AlgVector<T> correction;
        //! Level residual workspace.
        AlgVector<T> residual;
        //! General SpMV workspace.
        AlgVector<T> work;
    };

    //! Prepare canonical rows, validation data, and work vectors for \p level.
    void initialize_level (Level& level);
    //! Build the compact strength graph and its transpose for \p level.
    void build_strength (Level& level);
    //! Compute a deterministic PMIS C/F splitting for \p level.
    void split_pmis (Level& level);
    /**
     * \brief Construct matrix-based Extended+i interpolation and its
     * transpose restriction for \p level.
     *
     * The interpolation builder extracts strong fine-to-fine and
     * fine-to-coarse couplings, applies the Extended+i row scaling to the
     * fine-to-fine factor, and forms interpolation with one sparse matrix
     * product.
     */
    void build_interpolation (Level& level);
    /**
     * \brief Retain the largest interpolation entries and preserve row sums.
     *
     * Entries are ranked by decreasing magnitude, with global column ID as a
     * deterministic tie breaker. Each retained row is rescaled to the
     * original row sum. If the retained entries cancel exactly, the row-sum
     * correction is added to the first retained entry.
     *
     * \param source Sorted, duplicate-free interpolation rows.
     * \param max_elements Maximum entries retained in each row.
     * \return Truncated interpolation rows in sorted CSR form.
     */
    static csr_type truncate_interpolation (csr_type const& source,
                                             int max_elements);

private:

    //! Form and append the Galerkin coarse matrix below \p level.
    void build_next_level (Level& level);
    //! Gather, pad, and factor the globally coarsest matrix.
    void factor_coarse_matrix ();
    //! Recompute hierarchy complexity and persistent-storage diagnostics.
    void update_diagnostics (double setup_seconds);
    /** Apply the configured smoother to one level. */
    void smooth (Level& level, AlgVector<T>& x, AlgVector<T> const& b,
                 bool zero_initial_guess);
    /**
     * \brief Apply a recursive V(1,1)-cycle beginning at level \p ilev.
     *
     * \param ilev Hierarchy level index.
     * \param x Correction vector updated in place.
     * \param b Right-hand side on level \p ilev.
     */
public: // NOLINT  Private function, but public for CUDA

    void vcycle (int ilev, AlgVector<T>& x, AlgVector<T> const& b);

private:

    /**
     * \brief Solve the replicated dense coarse system.
     *
     * \param x Local portion of the coarse solution.
     * \param b Local portion of the coarse right-hand side.
     */
    void solve_coarse (AlgVector<T>& x, AlgVector<T> const& b) const;

    /**
     * \brief Copy pinned-host CSR storage into device-accessible CSR storage.
     *
     * \param src Source CSR in pinned host memory.
     * \return A device-accessible copy of \p src.
     */
    static csr_type copy_to_device (host_csr_type const& src);

    /**
     * \brief Copy a contiguous device field into pinned host memory.
     *
     * \tparam U Field value type.
     * \param src Source device pointer.
     * \param n Number of values to copy.
     * \return Pinned-host copy of the field.
     */
    template <typename U>
    static Gpu::PinnedVector<U>
    copy_device_to_host (U const* src, Long n);

    /**
     * \brief Gather a distributed device field into pinned host memory on
     * every rank.
     *
     * \tparam U Field value type with an AMReX MPI type mapping.
     * \param local Local device field.
     * \param partition Distribution of \p local across ranks.
     * \return Replicated global field in pinned host memory.
     */
    template <typename U>
    static Gpu::PinnedVector<U>
    gather_device_to_host (U const* local, AlgPartition const& partition);

    /**
     * \brief Build the global coarse partition from one local point count.
     *
     * \param local_coarse_size Number of coarse points owned by this rank.
     * \return Prefix partition for the next coarser level.
     */
    static AlgPartition make_coarse_partition (Long local_coarse_size);

    /**
     * \brief Compute the deterministic fractional priority for one global
     * graph vertex.
     *
     * \param gid Global vertex ID.
     * \param seed User-supplied priority seed.
     * \return Deterministic 64-bit hash priority.
     */
    AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    static unsigned long long priority_hash (Long gid, int seed) noexcept
    {
        auto x = static_cast<unsigned long long>(gid)
            ^ (static_cast<unsigned long long>(
                   static_cast<unsigned int>(seed)) << 32);
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    /**
     * \brief Compare two PMIS vertices by influence, hash, and global ID.
     *
     * \param lhs First global vertex ID.
     * \param rhs Second global vertex ID.
     * \param lhs_influence Incoming-strength degree of \p lhs.
     * \param rhs_influence Incoming-strength degree of \p rhs.
     * \param seed User-supplied priority seed.
     * \return `true` when \p lhs has higher deterministic priority.
     */
    AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    static bool has_higher_priority (Long lhs, Long rhs,
                                     Long lhs_influence,
                                     Long rhs_influence,
                                     int seed) noexcept
    {
        if (lhs_influence != rhs_influence) {
            return lhs_influence > rhs_influence;
        }
        auto const hl = priority_hash(lhs, seed);
        auto const hr = priority_hash(rhs, seed);
        return (hl != hr) ? (hl > hr) : (lhs > rhs);
    }

    SpMatrix<T> const& m_fine_matrix;
    Options m_options;
    Vector<std::unique_ptr<Level>> m_levels;
    LUSolver<dense_coarse_size,T> m_coarse_solver;
    Long m_coarse_active_size = 0;
    //! Whether the terminal level is small enough for the dense coarse solve.
    bool m_use_dense_coarse_solve = true;
    bool m_is_setup = false;
    int m_last_iterations = 0;
    T m_last_residual_norm = std::numeric_limits<T>::infinity();
    Diagnostics m_diagnostics;
};

template <typename T>
auto AMG<T>::copy_to_device (host_csr_type const& src) -> csr_type
{
    csr_type dst;
    duplicateCSR(Gpu::hostToDevice, dst, src);
    Gpu::streamSynchronize();
    return dst;
}

template <typename T>
template <typename U>
Gpu::PinnedVector<U>
AMG<T>::copy_device_to_host (U const* src, Long n)
{
    Gpu::PinnedVector<U> result(n);
    if (n == 0) {
        return result;
    }
#ifdef AMREX_USE_GPU
    Gpu::copyAsync(Gpu::deviceToHost, src, src+n, result.begin());
    Gpu::streamSynchronize();
#else
    std::copy(src, src+n, result.begin());
#endif
    return result;
}

template <typename T>
template <typename U>
Gpu::PinnedVector<U>
AMG<T>::gather_device_to_host (U const* local,
                               AlgPartition const& partition)
{
    int const myproc = ParallelDescriptor::MyProc();
    int const nprocs = ParallelDescriptor::NProcs();
    Long const nlocal = partition[myproc+1] - partition[myproc];
    auto hlocal = copy_device_to_host(local, nlocal);

    Gpu::PinnedVector<U> global(partition.numGlobalRows());
#ifndef AMREX_USE_MPI
    std::copy(hlocal.begin(), hlocal.end(), global.begin());
#else
    Vector<int> counts(nprocs);
    Vector<int> displs(nprocs);
    for (int p = 0; p < nprocs; ++p) {
        auto const count = partition[p+1] - partition[p];
        AMREX_ALWAYS_ASSERT(count <= std::numeric_limits<int>::max());
        AMREX_ALWAYS_ASSERT(partition[p] <= std::numeric_limits<int>::max());
        counts[p] = static_cast<int>(count);
        displs[p] = static_cast<int>(partition[p]);
    }
    auto const mpi_type = ParallelDescriptor::Mpi_typemap<U>::type();
    BL_MPI_REQUIRE(MPI_Allgatherv(
        hlocal.data(), counts[myproc], mpi_type,
        global.data(), counts.data(), displs.data(), mpi_type,
        ParallelContext::CommunicatorSub()));
#endif
    return global;
}

template <typename T>
AlgPartition AMG<T>::make_coarse_partition (Long local_coarse_size)
{
    int const nprocs = ParallelDescriptor::NProcs();
    Vector<Long> counts(nprocs);
#ifndef AMREX_USE_MPI
    counts[0] = local_coarse_size;
#else
    auto const mpi_long = ParallelDescriptor::Mpi_typemap<Long>::type();
    BL_MPI_REQUIRE(MPI_Allgather(
        &local_coarse_size, 1, mpi_long,
        counts.data(), 1, mpi_long,
        ParallelContext::CommunicatorSub()));
#endif
    Vector<Long> rows(nprocs+1, Long(0));
    for (int p = 0; p < nprocs; ++p) {
        rows[p+1] = rows[p] + counts[p];
    }
    return AlgPartition(std::move(rows));
}

template <typename T>
void AMG<T>::initialize_level (Level& level)
{
    auto const partition = level.A->partition();
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        level.A->columnPartition().empty()
        || level.A->columnPartition() == partition,
        "AMG setup requires identical row and column partitions");
    Long const global_rows = level.A->numGlobalRows();
    Long const local_rows = level.A->numLocalRows();

    auto host_rows = helper_type::copy_local_global_csr(*level.A);
    level.rows = copy_to_device(host_rows);

    if (m_options.smoother == Smoother::l1_jacobi) {
        level.l1_jacobi_smoother =
            std::make_unique<L1JacobiSmoother<T>>(*level.A);
    } else {
        typename HypreChebyshevSmoother<T>::Options smoother_options;
        smoother_options.order = m_options.chebyshev_order;
        smoother_options.eigenvalue_iterations =
            m_options.chebyshev_eigenvalue_iterations;
        smoother_options.fraction = m_options.chebyshev_fraction;
        smoother_options.variant = m_options.chebyshev_variant;
        smoother_options.scale = m_options.chebyshev_scale;
        level.chebyshev_smoother =
            std::make_unique<HypreChebyshevSmoother<T>>(
                *level.A, smoother_options);
    }
    level.rhs.define(partition);
    level.correction.define(partition);
    level.residual.define(partition);
    level.work.define(partition);

    auto const* AMREX_RESTRICT row = level.rows.row_offset.data();
    auto const* AMREX_RESTRICT col = level.rows.col_index.data();
    auto const* AMREX_RESTRICT val = level.rows.mat.data();
    auto const row_begin = level.A->globalRowBegin();

    int failures = Reduce::Sum<int>(
        local_rows,
        [=] AMREX_GPU_DEVICE (Long i) noexcept -> int
        {
            Long const global_row = i + row_begin;
            int diagonal_count = 0;
            T diagonal = T(0);
            T row_l1 = T(0);
            int bad_column = 0;
            for (Long p = row[i]; p < row[i+1]; ++p) {
                auto const j = col[p];
                auto const a = val[p];
                row_l1 += Math::abs(a);
                bad_column += (j < 0 || j >= global_rows);
                if (j == global_row) {
                    ++diagonal_count;
                    diagonal = a;
                }
            }
            return bad_column
                + (diagonal_count != 1)
                + (diagonal <= T(0))
                + (row_l1 <= T(0));
        });
    ParallelAllReduce::Sum(failures, ParallelContext::CommunicatorSub());
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        failures == 0,
        "AMG setup requires a square scalar matrix with global column IDs "
        "in range, exactly one positive diagonal per row, and nonzero L1 rows");
}

template <typename T>
void AMG<T>::build_strength (Level& level)
{
    Long const nrows = level.A->numLocalRows();
    auto const row_begin = level.A->globalRowBegin();
    auto const* AMREX_RESTRICT arow = level.rows.row_offset.data();
    auto const* AMREX_RESTRICT acol = level.rows.col_index.data();
    auto const* AMREX_RESTRICT aval = level.rows.mat.data();
    T const theta = m_options.strong_threshold;
    T const max_row_sum = m_options.max_row_sum;

    Gpu::DeviceVector<Long> counts(nrows);
    auto* AMREX_RESTRICT pcounts = counts.data();
    ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        auto const global_row = i + row_begin;
        T row_max = T(0);
        T diagonal = T(0);
        T row_sum = T(0);
        for (Long p = arow[i]; p < arow[i+1]; ++p) {
            row_sum += aval[p];
            if (acol[p] == global_row) {
                diagonal = aval[p];
            } else {
                row_max = amrex::max(row_max, -aval[p]);
            }
        }
        Long count = 0;
        bool const all_weak = max_row_sum < T(1)
            && Math::abs(row_sum) > Math::abs(diagonal)*max_row_sum;
        if (!all_weak && row_max > T(0)) {
            T const threshold = theta*row_max;
            for (Long p = arow[i]; p < arow[i+1]; ++p) {
                count += (acol[p] != global_row && -aval[p] >= threshold);
            }
        }
        pcounts[i] = count;
    });

    csr_type graph;
    graph.row_offset.resize(nrows+1);
    auto* AMREX_RESTRICT grow = graph.row_offset.data();
    Long const graph_nnz = Scan::PrefixSum<Long>(
        nrows,
        [=] AMREX_GPU_DEVICE (Long i) noexcept -> Long {
            return pcounts[i];
        },
        [=] AMREX_GPU_DEVICE (Long i, Long x) noexcept {
            grow[i] = x;
        },
        Scan::Type::exclusive, Scan::retSum);
    graph.nnz = graph_nnz;
    graph.mat.resize(graph_nnz);
    graph.col_index.resize(graph_nnz);

    auto* AMREX_RESTRICT gval = graph.mat.data();
    auto* AMREX_RESTRICT gcol = graph.col_index.data();
    ParallelFor(nrows+1, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        if (i == nrows) {
            grow[nrows] = graph_nnz;
            return;
        }
        auto const global_row = i + row_begin;
        T row_max = T(0);
        T diagonal = T(0);
        T row_sum = T(0);
        for (Long p = arow[i]; p < arow[i+1]; ++p) {
            row_sum += aval[p];
            if (acol[p] == global_row) {
                diagonal = aval[p];
            } else {
                row_max = amrex::max(row_max, -aval[p]);
            }
        }
        Long out = grow[i];
        bool const all_weak = max_row_sum < T(1)
            && Math::abs(row_sum) > Math::abs(diagonal)*max_row_sum;
        if (!all_weak && row_max > T(0)) {
            T const threshold = theta*row_max;
            for (Long p = arow[i]; p < arow[i+1]; ++p) {
                if (acol[p] != global_row && -aval[p] >= threshold) {
                    gcol[out] = acol[p];
                    gval[out] = T(1);
                    ++out;
                }
            }
        }
    });
    Gpu::streamSynchronize();

    duplicateCSR(Gpu::deviceToDevice, level.strength_rows, graph);
    level.S = std::make_unique<matrix_type>(
        level.A->partition(), level.A->partition(), std::move(graph));
    level.ST = std::make_unique<matrix_type>(
        transpose(*level.S, level.A->partition()));

    auto host_strength = helper_type::copy_local_global_csr(*level.S);
    auto host_transpose = helper_type::copy_local_global_csr(*level.ST);
    level.strength_transpose_rows = copy_to_device(host_transpose);

    Vector<Long> requested_ids;
    requested_ids.reserve(
        static_cast<std::size_t>(host_strength.nnz
                                 + host_transpose.nnz));
    requested_ids.insert(requested_ids.end(),
                         host_strength.col_index.begin(),
                         host_strength.col_index.end());
    requested_ids.insert(requested_ids.end(),
                         host_transpose.col_index.begin(),
                         host_transpose.col_index.end());
    level.halo = std::make_unique<AlgHaloExchange>(
        level.A->partition(), std::move(requested_ids));

    Gpu::PinnedVector<Long> strength_slots(host_strength.nnz);
    for (Long p = 0; p < host_strength.nnz; ++p) {
        strength_slots[p] =
            level.halo->localOrGhostIndex(host_strength.col_index[p]);
    }
    Gpu::PinnedVector<Long> transpose_slots(host_transpose.nnz);
    for (Long p = 0; p < host_transpose.nnz; ++p) {
        transpose_slots[p] =
            level.halo->localOrGhostIndex(host_transpose.col_index[p]);
    }
    level.strength_col_slots.resize(host_strength.nnz);
    level.strength_transpose_col_slots.resize(host_transpose.nnz);
    if (host_strength.nnz > 0) {
        Gpu::copyAsync(Gpu::hostToDevice, strength_slots.begin(),
                       strength_slots.end(),
                       level.strength_col_slots.begin());
    }
    if (host_transpose.nnz > 0) {
        Gpu::copyAsync(Gpu::hostToDevice, transpose_slots.begin(),
                       transpose_slots.end(),
                       level.strength_transpose_col_slots.begin());
    }
    Gpu::streamSynchronize();
}

template <typename T>
void AMG<T>::split_pmis (Level& level)
{
    Long const nlocal = level.A->numLocalRows();
    Long const row_begin = level.A->globalRowBegin();

    level.cf_marker.resize(nlocal);
    auto* AMREX_RESTRICT marker = level.cf_marker.data();

    Gpu::DeviceVector<Long> influence(nlocal);
    auto* AMREX_RESTRICT pinfluence = influence.data();
    auto const* AMREX_RESTRICT strow =
        level.strength_transpose_rows.row_offset.data();
    ParallelFor(nlocal, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        marker[i] = undecided;
        pinfluence[i] = strow[i+1] - strow[i];
    });

    auto halo_influence = level.halo->exchange(influence.data());
    auto const* AMREX_RESTRICT all_influence = halo_influence.data();

    Gpu::DeviceVector<Long> active(nlocal);
    Gpu::DeviceVector<Long> next_active(nlocal);
    auto* AMREX_RESTRICT active_rows = active.data();
    auto* AMREX_RESTRICT next_rows = next_active.data();
    ParallelFor(nlocal, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        active_rows[i] = i;
    });

    Gpu::DeviceVector<int> candidate(nlocal);
    auto* AMREX_RESTRICT is_candidate = candidate.data();
    Long active_count = nlocal;

    auto const* AMREX_RESTRICT srow =
        level.strength_rows.row_offset.data();
    auto const* AMREX_RESTRICT scol =
        level.strength_rows.col_index.data();
    auto const* AMREX_RESTRICT stcol =
        level.strength_transpose_rows.col_index.data();
    auto const* AMREX_RESTRICT sslot =
        level.strength_col_slots.data();
    auto const* AMREX_RESTRICT stslot =
        level.strength_transpose_col_slots.data();
    int const seed = m_options.priority_seed;

    Long global_active_count = active_count;
    ParallelAllReduce::Sum(global_active_count,
                           ParallelContext::CommunicatorSub());
    while (global_active_count > 0) {
        auto halo_marker = level.halo->exchange(marker);
        auto const* AMREX_RESTRICT all_marker = halo_marker.data();

        ParallelFor(nlocal, [=] AMREX_GPU_DEVICE (Long i) noexcept
        {
            is_candidate[i] = 0;
        });
        ParallelFor(active_count,
                    [=] AMREX_GPU_DEVICE (Long iactive) noexcept
        {
            Long const i = active_rows[iactive];
            Long const gid = i + row_begin;
            bool selected = true;
            for (Long p = srow[i]; p < srow[i+1] && selected; ++p) {
                Long const j = scol[p];
                Long const slot = sslot[p];
                if (all_marker[slot] == undecided
                    && has_higher_priority(
                        j, gid, all_influence[slot],
                        all_influence[i], seed))
                {
                    selected = false;
                }
            }
            for (Long p = strow[i]; p < strow[i+1] && selected; ++p) {
                Long const j = stcol[p];
                Long const slot = stslot[p];
                if (all_marker[slot] == undecided
                    && has_higher_priority(
                        j, gid, all_influence[slot],
                        all_influence[i], seed))
                {
                    selected = false;
                }
            }
            is_candidate[i] = selected ? 1 : 0;
        });

        auto global_candidate =
            level.halo->exchange(candidate.data());
        auto const* AMREX_RESTRICT all_candidate =
            global_candidate.data();

        ParallelFor(active_count,
                    [=] AMREX_GPU_DEVICE (Long iactive) noexcept
        {
            Long const i = active_rows[iactive];
            if (is_candidate[i] != 0) {
                marker[i] = coarse;
                return;
            }
            bool depends_on_new_coarse = false;
            for (Long p = srow[i];
                 p < srow[i+1] && !depends_on_new_coarse; ++p)
            {
                depends_on_new_coarse =
                    all_candidate[sslot[p]] != 0;
            }
            if (depends_on_new_coarse) {
                marker[i] = fine;
            }
        });

        Long const next_count = Scan::PrefixSum<Long>(
            active_count,
            [=] AMREX_GPU_DEVICE (Long iactive) noexcept -> Long {
                return marker[active_rows[iactive]] == undecided;
            },
            [=] AMREX_GPU_DEVICE (Long iactive, Long offset) noexcept {
                Long const i = active_rows[iactive];
                if (marker[i] == undecided) {
                    next_rows[offset] = i;
                }
            },
            Scan::Type::exclusive, Scan::retSum);
        std::swap(active, next_active);
        active_rows = active.data();
        next_rows = next_active.data();
        active_count = next_count;
        global_active_count = active_count;
        ParallelAllReduce::Sum(global_active_count,
                               ParallelContext::CommunicatorSub());
    }

    auto halo_marker = level.halo->exchange(marker);
    auto const* AMREX_RESTRICT all_marker = halo_marker.data();
    int coverage_failures = Reduce::Sum<int>(
        nlocal, [=] AMREX_GPU_DEVICE (Long i) noexcept -> int
        {
            if (marker[i] == coarse) {
                return 0;
            }
            bool covered = false;
            for (Long p = srow[i]; p < srow[i+1] && !covered; ++p) {
                covered = all_marker[sslot[p]] == coarse;
            }
            return covered ? 0 : 1;
        });
    ParallelAllReduce::Sum(coverage_failures,
                           ParallelContext::CommunicatorSub());
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        coverage_failures == 0,
        "AMG PMIS left a fine point without a strongly influencing "
        "coarse neighbor");

    Gpu::DeviceVector<Long> coarse_scan(nlocal);
    auto* AMREX_RESTRICT scan = coarse_scan.data();
    Long const local_coarse = Scan::PrefixSum<Long>(
        nlocal,
        [=] AMREX_GPU_DEVICE (Long i) noexcept -> Long {
            return marker[i] == coarse;
        },
        [=] AMREX_GPU_DEVICE (Long i, Long offset) noexcept {
            scan[i] = offset;
        },
        Scan::Type::exclusive, Scan::retSum);
    level.coarse_partition = make_coarse_partition(local_coarse);

    int const myproc = ParallelDescriptor::MyProc();
    Long const coarse_begin = level.coarse_partition[myproc];
    level.coarse_id.resize(nlocal);
    auto* AMREX_RESTRICT coarse_id = level.coarse_id.data();
    ParallelFor(nlocal, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        coarse_id[i] = (marker[i] == coarse)
            ? coarse_begin + scan[i] : Long(-1);
    });
    Gpu::streamSynchronize();
}

template <typename T>
auto AMG<T>::truncate_interpolation (csr_type const& source,
                                     int max_elements) -> csr_type
{
    AMREX_ALWAYS_ASSERT(max_elements > 0);
    Long const nrows = source.nrows();
    auto const* AMREX_RESTRICT source_row = source.row_offset.data();
    auto const* AMREX_RESTRICT source_col = source.col_index.data();
    auto const* AMREX_RESTRICT source_val = source.mat.data();

    Gpu::DeviceVector<Long> entry_rank(source.nnz);
    Gpu::DeviceVector<Long> retained_counts(nrows);
    auto* AMREX_RESTRICT ranks = entry_rank.data();
    auto* AMREX_RESTRICT counts = retained_counts.data();
    ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        Long retained = 0;
        for (Long p = source_row[i]; p < source_row[i+1]; ++p) {
            T const magnitude = Math::abs(source_val[p]);
            Long rank = 0;
            for (Long q = source_row[i]; q < source_row[i+1]; ++q) {
                T const other_magnitude = Math::abs(source_val[q]);
                rank += other_magnitude > magnitude
                    || (other_magnitude == magnitude
                        && source_col[q] < source_col[p]);
            }
            ranks[p] = rank;
            retained += rank < max_elements;
        }
        counts[i] = retained;
    });

    csr_type result;
    result.row_offset.resize(nrows+1);
    auto* AMREX_RESTRICT result_row = result.row_offset.data();
    Long const retained_nnz = Scan::PrefixSum<Long>(
        nrows,
        [=] AMREX_GPU_DEVICE (Long i) noexcept -> Long {
            return counts[i];
        },
        [=] AMREX_GPU_DEVICE (Long i, Long offset) noexcept {
            result_row[i] = offset;
        },
        Scan::Type::exclusive, Scan::retSum);
    result.nnz = retained_nnz;
    result.mat.resize(retained_nnz);
    result.col_index.resize(retained_nnz);
    auto* AMREX_RESTRICT result_val = result.mat.data();
    auto* AMREX_RESTRICT result_col = result.col_index.data();

    ParallelFor(nrows+1, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        if (i == nrows) {
            result_row[nrows] = retained_nnz;
            return;
        }

        T original_sum = T(0);
        T retained_sum = T(0);
        for (Long p = source_row[i]; p < source_row[i+1]; ++p) {
            original_sum += source_val[p];
            if (ranks[p] < max_elements) {
                retained_sum += source_val[p];
            }
        }
        T const scale = retained_sum != T(0)
            ? original_sum/retained_sum : T(1);
        T const correction = retained_sum == T(0)
            ? original_sum : T(0);

        Long out = result_row[i];
        for (Long p = source_row[i]; p < source_row[i+1]; ++p) {
            if (ranks[p] < max_elements) {
                result_col[out] = source_col[p];
                result_val[out] = scale*source_val[p]
                    + (out == result_row[i] ? correction : T(0));
                ++out;
            }
        }
    });
    Gpu::streamSynchronize();
    return result;
}

template <typename T>
void AMG<T>::build_interpolation (Level& level)
{
    Long const nrows = level.A->numLocalRows();
    Long const row_begin = level.A->globalRowBegin();
    auto const partition = level.A->partition();
    auto halo_marker =
        level.halo->exchange(level.cf_marker.data());
    auto halo_coarse_id =
        level.halo->exchange(level.coarse_id.data());

    auto const* AMREX_RESTRICT marker = level.cf_marker.data();
    auto const* AMREX_RESTRICT all_marker = halo_marker.data();
    auto const* AMREX_RESTRICT all_coarse_id =
        halo_coarse_id.data();
    auto const* AMREX_RESTRICT arow = level.rows.row_offset.data();
    auto const* AMREX_RESTRICT acol = level.rows.col_index.data();
    auto const* AMREX_RESTRICT aval = level.rows.mat.data();
    auto const* AMREX_RESTRICT srow =
        level.strength_rows.row_offset.data();
    auto const* AMREX_RESTRICT scol =
        level.strength_rows.col_index.data();
    auto const* AMREX_RESTRICT sslot =
        level.strength_col_slots.data();

    Gpu::DeviceVector<Long> ff_counts(nrows);
    Gpu::DeviceVector<Long> fc_counts(nrows);
    auto* AMREX_RESTRICT pff_counts = ff_counts.data();
    auto* AMREX_RESTRICT pfc_counts = fc_counts.data();
    ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        if (marker[i] == coarse) {
            pff_counts[i] = 1;
            pfc_counts[i] = 1;
        } else {
            Long ff_count = 1;
            Long fc_count = 0;
            for (Long p = srow[i]; p < srow[i+1]; ++p) {
                if (all_marker[sslot[p]] == fine) {
                    ++ff_count;
                } else {
                    ++fc_count;
                }
            }
            pff_counts[i] = ff_count;
            pfc_counts[i] = fc_count;
        }
    });

    Long missing = Reduce::Sum<Long>(
        nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept -> Long
        {
            return marker[i] == fine && pfc_counts[i] == 0;
        });
    ParallelAllReduce::Sum(missing, ParallelContext::CommunicatorSub());
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        missing == 0,
        "AMG Extended+i interpolation encountered a fine row without a "
        "strong coarse connection");

    csr_type scaled_ff;
    scaled_ff.row_offset.resize(nrows+1);
    auto* AMREX_RESTRICT ffrow = scaled_ff.row_offset.data();
    Long const ff_nnz = Scan::PrefixSum<Long>(
        nrows,
        [=] AMREX_GPU_DEVICE (Long i) noexcept -> Long {
            return pff_counts[i];
        },
        [=] AMREX_GPU_DEVICE (Long i, Long offset) noexcept {
            ffrow[i] = offset;
        },
        Scan::Type::exclusive, Scan::retSum);
    scaled_ff.nnz = ff_nnz;
    scaled_ff.mat.resize(ff_nnz);
    scaled_ff.col_index.resize(ff_nnz);
    auto* AMREX_RESTRICT ffval = scaled_ff.mat.data();
    auto* AMREX_RESTRICT ffcol = scaled_ff.col_index.data();

    csr_type strong_fc;
    strong_fc.row_offset.resize(nrows+1);
    auto* AMREX_RESTRICT fcrow = strong_fc.row_offset.data();
    Long const fc_nnz = Scan::PrefixSum<Long>(
        nrows,
        [=] AMREX_GPU_DEVICE (Long i) noexcept -> Long {
            return pfc_counts[i];
        },
        [=] AMREX_GPU_DEVICE (Long i, Long offset) noexcept {
            fcrow[i] = offset;
        },
        Scan::Type::exclusive, Scan::retSum);
    strong_fc.nnz = fc_nnz;
    strong_fc.mat.resize(fc_nnz);
    strong_fc.col_index.resize(fc_nnz);
    auto* AMREX_RESTRICT fcval = strong_fc.mat.data();
    auto* AMREX_RESTRICT fccol = strong_fc.col_index.data();

    ParallelFor(1, [=] AMREX_GPU_DEVICE (Long) noexcept
    {
        ffrow[nrows] = ff_nnz;
        fcrow[nrows] = fc_nnz;
    });
    Gpu::streamSynchronize();

    Gpu::DeviceVector<Long> ff_slots(ff_nnz);
    Gpu::DeviceVector<T> weak_diagonal_sum(nrows);
    Gpu::DeviceVector<T> strong_coarse_sum(nrows);
    auto* AMREX_RESTRICT pff_slots = ff_slots.data();
    auto* AMREX_RESTRICT pweak_diagonal_sum =
        weak_diagonal_sum.data();
    auto* AMREX_RESTRICT pstrong_coarse_sum =
        strong_coarse_sum.data();

    int invalid_structure = Reduce::Sum<int>(
        nrows,
        [=] AMREX_GPU_DEVICE (Long i) noexcept -> int
        {
            if (marker[i] == coarse) {
                ffcol[ffrow[i]] = i + row_begin;
                ffval[ffrow[i]] = T(1);
                pff_slots[ffrow[i]] = i;
                fccol[fcrow[i]] = all_coarse_id[i];
                fcval[fcrow[i]] = T(1);
                pweak_diagonal_sum[i] = T(0);
                pstrong_coarse_sum[i] = T(0);
                return 0;
            }

            Long const global_row = i + row_begin;
            Long ff_out = ffrow[i];
            Long fc_out = fcrow[i];
            T weak_and_diagonal = T(0);
            T coarse_sum = T(0);
            int diagonal_count = 0;
            for (Long p = arow[i]; p < arow[i+1]; ++p) {
                Long const j = acol[p];
                T const a = aval[p];
                if (j == global_row) {
                    ffcol[ff_out] = j;
                    ffval[ff_out] = a;
                    pff_slots[ff_out] = i;
                    ++ff_out;
                    weak_and_diagonal += a;
                    ++diagonal_count;
                    continue;
                }

                Long strong_entry = -1;
                for (Long sp = srow[i]; sp < srow[i+1]; ++sp) {
                    if (scol[sp] == j) {
                        strong_entry = sp;
                        break;
                    }
                }
                if (strong_entry < 0) {
                    weak_and_diagonal += a;
                    continue;
                }

                Long const slot = sslot[strong_entry];
                if (all_marker[slot] == fine) {
                    ffcol[ff_out] = j;
                    ffval[ff_out] = a;
                    pff_slots[ff_out] = slot;
                    ++ff_out;
                } else {
                    fccol[fc_out] = all_coarse_id[slot];
                    fcval[fc_out] = a;
                    ++fc_out;
                    coarse_sum += a;
                }
            }

            pweak_diagonal_sum[i] = weak_and_diagonal;
            pstrong_coarse_sum[i] = coarse_sum;
            return (diagonal_count != 1)
                + (ff_out != ffrow[i+1])
                + (fc_out != fcrow[i+1]);
        });
    ParallelAllReduce::Sum(invalid_structure,
                           ParallelContext::CommunicatorSub());
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        invalid_structure == 0,
        "AMG Extended+i interpolation could not extract its strong "
        "fine-point submatrices");

    Gpu::streamSynchronize();

    csr_type unscaled_ff;
    duplicateCSR(Gpu::deviceToDevice, unscaled_ff, scaled_ff);
    matrix_type ff_matrix(
        partition, partition, std::move(unscaled_ff));
    matrix_type ff_transpose = transpose(ff_matrix, partition);
    auto host_ff_transpose =
        helper_type::copy_local_global_csr(ff_transpose);
    auto ff_transpose_rows = copy_to_device(host_ff_transpose);

    auto halo_coarse_sum =
        level.halo->exchange(strong_coarse_sum.data());
    auto const* AMREX_RESTRICT all_coarse_sum =
        halo_coarse_sum.data();
    auto const* AMREX_RESTRICT ff_transpose_row =
        ff_transpose_rows.row_offset.data();
    auto const* AMREX_RESTRICT ff_transpose_col =
        ff_transpose_rows.col_index.data();
    auto const* AMREX_RESTRICT ff_transpose_val =
        ff_transpose_rows.mat.data();

    int invalid_scale = Reduce::Sum<int>(
        nrows,
        [=] AMREX_GPU_DEVICE (Long i) noexcept -> int
        {
            if (marker[i] == coarse) {
                return 0;
            }

            Long const global_row = i + row_begin;
            T theta = pweak_diagonal_sum[i];
            for (Long p = ffrow[i]; p < ffrow[i+1]; ++p) {
                Long const j = ffcol[p];
                if (j == global_row) {
                    ffval[p] = T(1);
                    continue;
                }

                T reverse = T(0);
                for (Long q = ff_transpose_row[i];
                     q < ff_transpose_row[i+1]; ++q)
                {
                    if (ff_transpose_col[q] == j) {
                        reverse = ff_transpose_val[q];
                        break;
                    }
                }

                T const original = ffval[p];
                T const denominator =
                    all_coarse_sum[pff_slots[p]] + reverse;
                if (denominator != T(0)) {
                    ffval[p] = original/denominator;
                    theta += reverse*ffval[p];
                } else {
                    ffval[p] = T(0);
                    theta += original;
                }
            }

            T const scale = theta != T(0) ? -T(1)/theta : -T(1);
            int invalid = 0;
            for (Long p = ffrow[i]; p < ffrow[i+1]; ++p) {
                ffval[p] *= scale;
                invalid += !Math::isfinite(ffval[p]);
            }
            return invalid;
        });
    ParallelAllReduce::Sum(invalid_scale,
                           ParallelContext::CommunicatorSub());
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        invalid_scale == 0,
        "AMG Extended+i interpolation produced a non-finite row scale");
    Gpu::streamSynchronize();

    matrix_type scaled_ff_matrix(
        partition, partition, std::move(scaled_ff));
    matrix_type strong_fc_matrix(
        partition, level.coarse_partition, std::move(strong_fc));
    auto interpolation = SpGEMM(
        scaled_ff_matrix, strong_fc_matrix, level.coarse_partition);
    if (m_options.max_interp_elements > 0) {
        AMREX_ALWAYS_ASSERT(!interpolation.m_split);
        auto truncated = truncate_interpolation(
            interpolation.m_csr, m_options.max_interp_elements);
        level.P = std::make_unique<matrix_type>(
            partition, level.coarse_partition, std::move(truncated));
    } else {
        level.P = std::make_unique<matrix_type>(std::move(interpolation));
    }
    level.R = std::make_unique<matrix_type>(
        transpose(*level.P, level.coarse_partition));
}

template <typename T>
void AMG<T>::build_next_level (Level& level)
{
    auto B = SpGEMM(*level.A, *level.P, level.coarse_partition);
    auto coarse_matrix =
        SpGEMM(*level.R, B, level.coarse_partition);

    auto next = std::make_unique<Level>();
    next->owned_A =
        std::make_unique<matrix_type>(std::move(coarse_matrix));
    next->A = next->owned_A.get();
    m_levels.push_back(std::move(next));
}

template <typename T>
void AMG<T>::factor_coarse_matrix ()
{
    auto const& level = *m_levels.back();
    auto global = helper_type::gather_global_rows(*level.A);
    m_coarse_active_size = level.A->numGlobalRows();
    AMREX_ALWAYS_ASSERT(
        m_coarse_active_size > 0
        && m_coarse_active_size <= dense_coarse_size);

    Array2D<T,0,dense_coarse_size-1,0,dense_coarse_size-1,Order::C>
        dense{};
    for (int i = 0; i < dense_coarse_size; ++i) {
        for (int j = 0; j < dense_coarse_size; ++j) {
            dense(i,j) = T(0);
        }
    }
    for (Long i = 0; i < m_coarse_active_size; ++i) {
        for (Long p = global.row_offset[i];
             p < global.row_offset[i+1]; ++p)
        {
            auto const j = global.col_index[p];
            AMREX_ALWAYS_ASSERT(
                j >= 0 && j < m_coarse_active_size);
            dense(static_cast<int>(i),static_cast<int>(j)) += global.mat[p];
        }
    }
    for (Long i = m_coarse_active_size;
         i < dense_coarse_size; ++i)
    {
        dense(static_cast<int>(i),static_cast<int>(i)) = T(1);
    }
    m_coarse_solver.define(dense);
}

template <typename T>
void AMG<T>::update_diagnostics (double setup_seconds)
{
    auto global_sum = [] (Long local) -> Long
    {
        ParallelAllReduce::Sum(
            local, ParallelContext::CommunicatorSub());
        return local;
    };
    auto csr_bytes = [] (csr_type const& csr) -> Long
    {
        return static_cast<Long>(
            csr.mat.size()*sizeof(T)
            + csr.col_index.size()*sizeof(Long)
            + csr.row_offset.size()*sizeof(Long));
    };
    auto matrix_bytes = [] (matrix_type const& matrix) -> Long
    {
        return matrix.numLocalNonZeros()
                *static_cast<Long>(sizeof(T)+sizeof(Long))
            +(matrix.numLocalRows()+1)*static_cast<Long>(sizeof(Long));
    };

    m_diagnostics = Diagnostics{};
    m_diagnostics.levels.resize(m_levels.size());
    Long total_rows = 0;
    Long total_nonzeros = 0;
    Long total_interpolation_nonzeros = 0;
    Long local_storage = static_cast<Long>(sizeof(m_coarse_solver));

    for (int ilev = 0; ilev < static_cast<int>(m_levels.size());
         ++ilev)
    {
        auto const& level = *m_levels[ilev];
        auto& info = m_diagnostics.levels[ilev];
        info.rows = level.A->numGlobalRows();
        info.nonzeros = global_sum(level.A->numLocalNonZeros());
        total_rows += info.rows;
        total_nonzeros += info.nonzeros;
        if (level.P) {
            info.interpolation_nonzeros =
                global_sum(level.P->numLocalNonZeros());
            total_interpolation_nonzeros +=
                info.interpolation_nonzeros;
        }
        if (ilev+1 < static_cast<int>(m_levels.size())) {
            info.coarse_fraction =
                static_cast<double>(
                    m_levels[ilev+1]->A->numGlobalRows())
                / static_cast<double>(info.rows);
        }

        if (level.owned_A) {
            local_storage += matrix_bytes(*level.owned_A);
        }
        if (level.S) {
            local_storage += matrix_bytes(*level.S);
        }
        if (level.ST) {
            local_storage += matrix_bytes(*level.ST);
        }
        if (level.P) {
            local_storage += matrix_bytes(*level.P);
        }
        if (level.R) {
            local_storage += matrix_bytes(*level.R);
        }
        local_storage += csr_bytes(level.rows)
            + csr_bytes(level.strength_rows)
            + csr_bytes(level.strength_transpose_rows)
            + static_cast<Long>(
                (level.strength_col_slots.size()
                 +level.strength_transpose_col_slots.size())
                *sizeof(Long))
            + static_cast<Long>(level.cf_marker.size()*sizeof(int))
            + static_cast<Long>(level.coarse_id.size()*sizeof(Long))
            + 4*level.A->numLocalRows()*static_cast<Long>(sizeof(T));
        if (level.l1_jacobi_smoother) {
            local_storage += level.A->numLocalRows()
                *static_cast<Long>(sizeof(T));
        }
        if (level.chebyshev_smoother) {
            local_storage += 5*level.A->numLocalRows()
                *static_cast<Long>(sizeof(T));
        }
        if (level.halo) {
            local_storage +=
                static_cast<Long>(level.halo->estimatedBytes());
        }
    }

    auto const fine_rows =
        static_cast<double>(m_diagnostics.levels.front().rows);
    auto const fine_nonzeros =
        static_cast<double>(m_diagnostics.levels.front().nonzeros);
    m_diagnostics.grid_complexity =
        static_cast<double>(total_rows)/fine_rows;
    m_diagnostics.operator_complexity =
        static_cast<double>(total_nonzeros)/fine_nonzeros;
    m_diagnostics.interpolation_complexity =
        static_cast<double>(total_interpolation_nonzeros)/fine_rows;
    m_diagnostics.hierarchy_storage_bytes =
        global_sum(local_storage);
    ParallelAllReduce::Max(
        setup_seconds, ParallelContext::CommunicatorSub());
    m_diagnostics.setup_seconds = setup_seconds;
}

template <typename T>
void AMG<T>::printDiagnostics () const
{
    auto os = amrex::Print();
    os << "AMG hierarchy: levels=" << m_diagnostics.levels.size()
       << ", grid complexity=" << m_diagnostics.grid_complexity
       << ", operator complexity=" << m_diagnostics.operator_complexity
       << ", interpolation complexity="
       << m_diagnostics.interpolation_complexity
       << ", setup=" << m_diagnostics.setup_seconds << " s"
       << ", last apply=" << m_diagnostics.last_apply_seconds << " s"
       << ", last solve=" << m_diagnostics.last_solve_seconds << " s"
       << ", persistent storage="
       << m_diagnostics.hierarchy_storage_bytes << " bytes\n";
    for (int ilev = 0;
         ilev < static_cast<int>(m_diagnostics.levels.size()); ++ilev)
    {
        auto const& info = m_diagnostics.levels[ilev];
        os << "  level " << ilev << ": rows=" << info.rows
           << ", nnz=" << info.nonzeros;
        if (ilev+1 < static_cast<int>(m_diagnostics.levels.size())) {
            os << ", P nnz=" << info.interpolation_nonzeros
               << ", coarse/fine=" << info.coarse_fraction;
        }
        os << '\n';
    }
}

template <typename T>
void AMG<T>::setup ()
{
    double const setup_start = amrex::second();
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_options.strong_threshold > T(0)
        && m_options.strong_threshold <= T(1),
        "AMG strong_threshold must be in (0,1]");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_options.max_row_sum > T(0)
        && m_options.max_row_sum <= T(1),
        "AMG max_row_sum must be in (0,1]");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_options.max_interp_elements >= 0,
        "AMG max_interp_elements must be nonnegative");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_options.max_levels > 0,
        "AMG max_levels must be positive");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_options.max_coarse_size > 0
        && m_options.max_coarse_size <= dense_coarse_size,
        "AMG max_coarse_size must be in [1,9]");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_options.pre_sweeps == 1 && m_options.post_sweeps == 1,
        "The first AMG implementation supports only V(1,1)");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_options.smoother == Smoother::l1_jacobi
        || m_options.smoother == Smoother::chebyshev,
        "AMG smoother must be L1-Jacobi or Chebyshev");
    if (m_options.smoother == Smoother::chebyshev) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            m_options.chebyshev_order >= 1
            && m_options.chebyshev_order <= 4,
            "AMG Chebyshev order must be in [1,4]");
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            m_options.chebyshev_eigenvalue_iterations >= 0,
            "AMG Chebyshev eigenvalue iterations must be nonnegative");
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            m_options.chebyshev_fraction >= T(0)
            && m_options.chebyshev_fraction <= T(1),
            "AMG Chebyshev fraction must be in [0,1]");
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            m_options.chebyshev_variant == 0
            || m_options.chebyshev_variant == 1,
            "AMG Chebyshev variant must be zero or one");
    }

    m_is_setup = false;
    m_use_dense_coarse_solve = true;
    m_coarse_active_size = 0;
    m_levels.clear();
    auto fine_level = std::make_unique<Level>();
    fine_level->A = &m_fine_matrix;
    m_levels.push_back(std::move(fine_level));

    for (int ilev = 0; ; ++ilev) {
        auto& level = *m_levels[ilev];
        initialize_level(level);
        Long const fine_size = level.A->numGlobalRows();
        if (fine_size <= m_options.max_coarse_size) {
            break;
        }
        if (ilev+1 >= m_options.max_levels) {
            m_use_dense_coarse_solve = false;
            break;
        }

        build_strength(level);
        split_pmis(level);
        Long const coarse_size =
            level.coarse_partition.numGlobalRows();
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            coarse_size > 0 && coarse_size <= fine_size,
            "AMG PMIS produced an invalid global coarse size");
        if (coarse_size == fine_size) {
            m_use_dense_coarse_solve = false;
            break;
        }
        build_interpolation(level);
        build_next_level(level);
    }

    if (m_use_dense_coarse_solve) {
        factor_coarse_matrix();
    }
    m_is_setup = true;
    update_diagnostics(amrex::second()-setup_start);
}

template <typename T>
void AMG<T>::solve_coarse (AlgVector<T>& x,
                           AlgVector<T> const& b) const
{
    auto global_rhs = gather_device_to_host(
        b.data(), b.partition());
    T padded_rhs[dense_coarse_size] = {};
    T padded_solution[dense_coarse_size] = {};
    for (Long i = 0; i < m_coarse_active_size; ++i) {
        padded_rhs[i] = global_rhs[i];
    }
    m_coarse_solver(padded_solution, padded_rhs);

    int const myproc = ParallelDescriptor::MyProc();
    Long const begin = x.partition()[myproc];
    Long const end = x.partition()[myproc+1];
    Gpu::PinnedVector<T> local_solution(end-begin);
    for (Long i = begin; i < end; ++i) {
        local_solution[i-begin] = padded_solution[i];
    }
    if (!local_solution.empty()) {
        Gpu::copyAsync(Gpu::hostToDevice,
                       local_solution.begin(), local_solution.end(),
                       x.data());
        Gpu::streamSynchronize();
    }
}

template <typename T>
void AMG<T>::smooth (Level& level, AlgVector<T>& x,
                     AlgVector<T> const& b, bool zero_initial_guess)
{
    if (m_options.smoother == Smoother::l1_jacobi) {
        AMREX_ALWAYS_ASSERT(level.l1_jacobi_smoother != nullptr);
        level.l1_jacobi_smoother->sweep(
            x, b, level.work, zero_initial_guess);
    } else {
        AMREX_ALWAYS_ASSERT(level.chebyshev_smoother != nullptr);
        level.chebyshev_smoother->sweep(x, b);
    }
}

template <typename T>
void AMG<T>::vcycle (int ilev, AlgVector<T>& x,
                     AlgVector<T> const& b)
{
    auto& level = *m_levels[ilev];
    if (ilev+1 == static_cast<int>(m_levels.size())) {
        if (m_use_dense_coarse_solve) {
            solve_coarse(x, b);
        } else {
            smooth(level, x, b, true);
        }
        return;
    }

    Long const nrows = x.numLocalRows();
    smooth(level, x, b, true);

    SpMV(level.work, *level.A, x);
    auto const* AMREX_RESTRICT pb = b.data();
    auto const* AMREX_RESTRICT pax = level.work.data();
    auto* AMREX_RESTRICT pres = level.residual.data();
    ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        pres[i] = pb[i] - pax[i];
    });

    auto& next = *m_levels[ilev+1];
    SpMV(next.rhs, *level.R, level.residual);
    next.correction.setValAsync(T(0));
    vcycle(ilev+1, next.correction, next.rhs);

    SpMV(level.work, *level.P, next.correction);
    x.plusAsync(level.work);

    smooth(level, x, b, false);
    Gpu::streamSynchronize();
}

template <typename T>
void AMG<T>::apply (AlgVector<T>& z, AlgVector<T> const& r)
{
    double const apply_start = amrex::second();
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_is_setup, "AMG::setup must be called before AMG::apply");
    AMREX_ALWAYS_ASSERT(z.partition() == m_fine_matrix.partition());
    AMREX_ALWAYS_ASSERT(r.partition() == m_fine_matrix.partition());
    auto& root = *m_levels.front();
    root.rhs.copyAsync(r);
    z.setValAsync(T(0));
    vcycle(0, z, root.rhs);
    m_diagnostics.last_apply_seconds =
        amrex::second()-apply_start;
}

template <typename T>
void AMG<T>::solve (AlgVector<T>& x, AlgVector<T> const& b,
                    T relative_tolerance, T absolute_tolerance,
                    int maximum_iterations)
{
    double const solve_start = amrex::second();
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_is_setup, "AMG::setup must be called before AMG::solve");
    AMREX_ALWAYS_ASSERT(relative_tolerance >= T(0));
    AMREX_ALWAYS_ASSERT(absolute_tolerance >= T(0));
    AMREX_ALWAYS_ASSERT(maximum_iterations >= 0);

    auto& root = *m_levels.front();
    SpMV(root.work, m_fine_matrix, x);
    LinComb(root.residual, T(1), b, T(-1), root.work);
    T const initial_norm = root.residual.norm2();
    T const target =
        amrex::max(absolute_tolerance,
                   relative_tolerance*initial_norm);

    m_last_iterations = 0;
    m_last_residual_norm = initial_norm;
    while (m_last_iterations < maximum_iterations
           && m_last_residual_norm > target)
    {
        apply(root.correction, root.residual);
        x.plusAsync(root.correction);
        SpMV(root.work, m_fine_matrix, x);
        LinComb(root.residual, T(1), b, T(-1), root.work);
        m_last_residual_norm = root.residual.norm2();
        ++m_last_iterations;
    }
    m_diagnostics.last_solve_seconds =
        amrex::second()-solve_start;
}

}

#endif

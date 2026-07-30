#ifndef AMREX_AMG_H_
#define AMREX_AMG_H_
#include <AMReX_Config.H>

#include <AMReX_AlgVecUtil.H>
#include <AMReX_LUSolver.H>
#include <AMReX_Math.H>
#include <AMReX_ParallelContext.H>
#include <AMReX_ParallelReduce.H>
#include <AMReX_Reduce.H>
#include <AMReX_Scan.H>
#include <AMReX_SpGEMM.H>
#include <AMReX_SpMatUtil.H>
#include <AMReX_SpMV.H>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace amrex {

/**
 * \file AMReX_AMG.H
 *
 * A deliberately narrow classical algebraic multigrid implementation for
 * scalar elliptic matrices. It uses classical strength, deterministic PMIS
 * C/F splitting, direct interpolation, Galerkin coarsening, L1-Jacobi
 * smoothing, and a dense solve on the last (at most nine point) level.
 */
/**
 * \brief Classical algebraic multigrid preconditioner for scalar elliptic
 * matrices.
 *
 * The hierarchy uses sign-based strength of connection, deterministic PMIS
 * C/F splitting, direct interpolation, transpose restriction, Galerkin
 * coarse operators, L1-Jacobi smoothing, and a dense coarse solve.
 *
 * \tparam T Floating-point matrix and vector scalar type.
 */
template <typename T>
class AMG
{
public:
    static_assert(std::is_floating_point_v<T>);

    /**
     * \brief Setup and cycle controls for AMG.
     */
    struct Options {
        //! Classical strength-of-connection threshold in the interval (0,1].
        T strong_threshold = T(0.25);
        //! Maximum number of hierarchy levels, including the finest level.
        int max_levels = 25;
        //! Largest global level factored by the dense coarse solver.
        int max_coarse_size = 9;
        //! Number of L1-Jacobi sweeps before coarse-grid correction.
        int pre_sweeps = 1;
        //! Number of L1-Jacobi sweeps after coarse-grid correction.
        int post_sweeps = 1;
        //! Seed used by the deterministic PMIS priority hash.
        int priority_seed = 0;
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

private:
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

        //! L1-Jacobi denominator for every local row.
        AlgVector<T> l1_norm;
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
    //! Construct direct interpolation and transpose restriction for \p level.
    void build_interpolation (Level& level);
    //! Form and append the Galerkin coarse matrix below \p level.
    void build_next_level (Level& level);
    //! Gather, pad, and factor the globally coarsest matrix.
    void factor_coarse_matrix ();
    /**
     * \brief Apply a recursive V(1,1)-cycle beginning at level \p ilev.
     *
     * \param ilev Hierarchy level index.
     * \param x Correction vector updated in place.
     * \param b Right-hand side on level \p ilev.
     */
    void vcycle (int ilev, AlgVector<T>& x, AlgVector<T> const& b);
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
     * \brief Gather and replicate a distributed field in device memory.
     *
     * \tparam U Field value type with an AMReX MPI type mapping.
     * \param local Local device field.
     * \param partition Distribution of \p local across ranks.
     * \return Replicated global field in device-accessible memory.
     */
    template <typename U>
    static Gpu::DeviceVector<U>
    gather_device_field (U const* local, AlgPartition const& partition);

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
     * \param influence Replicated incoming-strength degree array.
     * \param seed User-supplied priority seed.
     * \return `true` when \p lhs has higher deterministic priority.
     */
    AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    static bool has_higher_priority (Long lhs, Long rhs,
                                     Long const* influence,
                                     int seed) noexcept
    {
        if (influence[lhs] != influence[rhs]) {
            return influence[lhs] > influence[rhs];
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
    bool m_is_setup = false;
    int m_last_iterations = 0;
    T m_last_residual_norm = std::numeric_limits<T>::infinity();
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
template <typename U>
Gpu::DeviceVector<U>
AMG<T>::gather_device_field (U const* local,
                             AlgPartition const& partition)
{
    auto host = gather_device_to_host(local, partition);
    Gpu::DeviceVector<U> result(host.size());
    if (!host.empty()) {
        Gpu::copyAsync(Gpu::hostToDevice, host.begin(), host.end(),
                       result.begin());
        Gpu::streamSynchronize();
    }
    return result;
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
    Long const global_rows = level.A->numGlobalRows();
    Long const local_rows = level.A->numLocalRows();

    auto host_rows = helper_type::copy_local_global_csr(*level.A);
    level.rows = copy_to_device(host_rows);

    level.l1_norm.define(partition);
    level.rhs.define(partition);
    level.correction.define(partition);
    level.residual.define(partition);
    level.work.define(partition);

    auto const* AMREX_RESTRICT row = level.rows.row_offset.data();
    auto const* AMREX_RESTRICT col = level.rows.col_index.data();
    auto const* AMREX_RESTRICT val = level.rows.mat.data();
    auto* AMREX_RESTRICT l1 = level.l1_norm.data();
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
                bad_column += (j < 0 || j >= global_rows);
                row_l1 += amrex::Math::abs(a);
                if (j == global_row) {
                    ++diagonal_count;
                    diagonal = a;
                }
            }
            l1[i] = row_l1;
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

    Gpu::DeviceVector<Long> counts(nrows);
    auto* AMREX_RESTRICT pcounts = counts.data();
    ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        auto const global_row = i + row_begin;
        T row_max = T(0);
        for (Long p = arow[i]; p < arow[i+1]; ++p) {
            if (acol[p] != global_row) {
                row_max = amrex::max(row_max, -aval[p]);
            }
        }
        Long count = 0;
        if (row_max > T(0)) {
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
        for (Long p = arow[i]; p < arow[i+1]; ++p) {
            if (acol[p] != global_row) {
                row_max = amrex::max(row_max, -aval[p]);
            }
        }
        Long out = grow[i];
        if (row_max > T(0)) {
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
        level.A->partition(), std::move(graph));
    level.ST = std::make_unique<matrix_type>(
        transpose(*level.S, level.A->partition()));

    auto host_transpose = helper_type::copy_local_global_csr(*level.ST);
    level.strength_transpose_rows = copy_to_device(host_transpose);
}

template <typename T>
void AMG<T>::split_pmis (Level& level)
{
    Long const nlocal = level.A->numLocalRows();
    Long const row_begin = level.A->globalRowBegin();
    auto const partition = level.A->partition();

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

    auto global_influence = gather_device_field(
        influence.data(), partition);
    auto const* AMREX_RESTRICT all_influence = global_influence.data();

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
    int const seed = m_options.priority_seed;

    Long global_active_count = active_count;
    ParallelAllReduce::Sum(global_active_count,
                           ParallelContext::CommunicatorSub());
    while (global_active_count > 0) {
        auto global_marker = gather_device_field(marker, partition);
        auto const* AMREX_RESTRICT all_marker = global_marker.data();

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
                if (all_marker[j] == undecided
                    && has_higher_priority(j, gid, all_influence, seed))
                {
                    selected = false;
                }
            }
            for (Long p = strow[i]; p < strow[i+1] && selected; ++p) {
                Long const j = stcol[p];
                if (all_marker[j] == undecided
                    && has_higher_priority(j, gid, all_influence, seed))
                {
                    selected = false;
                }
            }
            is_candidate[i] = selected ? 1 : 0;
        });

        auto global_candidate = gather_device_field(
            candidate.data(), partition);
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
                depends_on_new_coarse = all_candidate[scol[p]] != 0;
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

    auto global_marker = gather_device_field(marker, partition);
    auto const* AMREX_RESTRICT all_marker = global_marker.data();
    int invariant_failures = Reduce::Sum<int>(
        nlocal, [=] AMREX_GPU_DEVICE (Long i) noexcept -> int
        {
            int failed = 0;
            if (marker[i] == coarse) {
                for (Long p = srow[i]; p < srow[i+1]; ++p) {
                    failed += (all_marker[scol[p]] == coarse);
                }
                for (Long p = strow[i]; p < strow[i+1]; ++p) {
                    failed += (all_marker[stcol[p]] == coarse);
                }
            } else {
                bool covered = false;
                for (Long p = srow[i]; p < srow[i+1] && !covered; ++p) {
                    covered = all_marker[scol[p]] == coarse;
                }
                failed += !covered;
            }
            return failed;
        });
    ParallelAllReduce::Sum(invariant_failures,
                           ParallelContext::CommunicatorSub());
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        invariant_failures == 0,
        "AMG PMIS failed coarse independence or fine-point interpolation "
        "coverage for the supplied strength graph");

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
void AMG<T>::build_interpolation (Level& level)
{
    Long const nrows = level.A->numLocalRows();
    Long const row_begin = level.A->globalRowBegin();
    auto const partition = level.A->partition();
    auto global_marker = gather_device_field(
        level.cf_marker.data(), partition);
    auto global_coarse_id = gather_device_field(
        level.coarse_id.data(), partition);

    auto const* AMREX_RESTRICT marker = level.cf_marker.data();
    auto const* AMREX_RESTRICT all_marker = global_marker.data();
    auto const* AMREX_RESTRICT all_coarse_id =
        global_coarse_id.data();
    auto const* AMREX_RESTRICT arow = level.rows.row_offset.data();
    auto const* AMREX_RESTRICT acol = level.rows.col_index.data();
    auto const* AMREX_RESTRICT aval = level.rows.mat.data();
    auto const* AMREX_RESTRICT srow =
        level.strength_rows.row_offset.data();
    auto const* AMREX_RESTRICT scol =
        level.strength_rows.col_index.data();

    Gpu::DeviceVector<Long> counts(nrows);
    auto* AMREX_RESTRICT pcounts = counts.data();
    ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        if (marker[i] == coarse) {
            pcounts[i] = 1;
        } else {
            Long count = 0;
            for (Long p = srow[i]; p < srow[i+1]; ++p) {
                count += all_marker[scol[p]] == coarse;
            }
            pcounts[i] = count;
        }
    });

    Long missing = Reduce::Sum<Long>(
        nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept -> Long
        {
            return marker[i] == fine && pcounts[i] == 0;
        });
    ParallelAllReduce::Sum(missing, ParallelContext::CommunicatorSub());
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        missing == 0,
        "AMG direct interpolation encountered a fine row without a "
        "strong coarse neighbor");

    csr_type interpolation;
    interpolation.row_offset.resize(nrows+1);
    auto* AMREX_RESTRICT prow = interpolation.row_offset.data();
    Long const pnnz = Scan::PrefixSum<Long>(
        nrows,
        [=] AMREX_GPU_DEVICE (Long i) noexcept -> Long {
            return pcounts[i];
        },
        [=] AMREX_GPU_DEVICE (Long i, Long offset) noexcept {
            prow[i] = offset;
        },
        Scan::Type::exclusive, Scan::retSum);
    interpolation.nnz = pnnz;
    interpolation.mat.resize(pnnz);
    interpolation.col_index.resize(pnnz);
    auto* AMREX_RESTRICT pval = interpolation.mat.data();
    auto* AMREX_RESTRICT pcol = interpolation.col_index.data();

    int invalid_scales = Reduce::Sum<int>(
        nrows,
        [=] AMREX_GPU_DEVICE (Long i) noexcept -> int
        {
            if (marker[i] == coarse) {
                pcol[prow[i]] = all_coarse_id[i + row_begin];
                pval[prow[i]] = T(1);
                return 0;
            }

            Long const global_row = i + row_begin;
            T diagonal = T(0);
            T negative_sum = T(0);
            T positive_sum = T(0);
            for (Long p = arow[i]; p < arow[i+1]; ++p) {
                auto const a = aval[p];
                if (acol[p] == global_row) {
                    diagonal = a;
                } else if (a < T(0)) {
                    negative_sum += a;
                } else if (a > T(0)) {
                    positive_sum += a;
                }
            }

            T coarse_negative_sum = T(0);
            T coarse_positive_sum = T(0);
            for (Long sp = srow[i]; sp < srow[i+1]; ++sp) {
                Long const j = scol[sp];
                if (all_marker[j] != coarse) {
                    continue;
                }
                T a = T(0);
                for (Long ap = arow[i]; ap < arow[i+1]; ++ap) {
                    if (acol[ap] == j) {
                        a = aval[ap];
                        break;
                    }
                }
                if (a < T(0)) {
                    coarse_negative_sum += a;
                } else if (a > T(0)) {
                    coarse_positive_sum += a;
                }
            }

            bool const need_negative = coarse_negative_sum != T(0);
            bool const need_positive = coarse_positive_sum != T(0);
            T const alpha = need_negative
                ? negative_sum/(diagonal*coarse_negative_sum) : T(0);
            T const beta = need_positive
                ? positive_sum/(diagonal*coarse_positive_sum) : T(0);

            Long out = prow[i];
            for (Long sp = srow[i]; sp < srow[i+1]; ++sp) {
                Long const j = scol[sp];
                if (all_marker[j] != coarse) {
                    continue;
                }
                T a = T(0);
                for (Long ap = arow[i]; ap < arow[i+1]; ++ap) {
                    if (acol[ap] == j) {
                        a = aval[ap];
                        break;
                    }
                }
                pcol[out] = all_coarse_id[j];
                pval[out] = (a < T(0)) ? -alpha*a : -beta*a;
                ++out;
            }
            return (diagonal == T(0))
                || (!need_negative && negative_sum != T(0));
        });
    ParallelAllReduce::Sum(invalid_scales,
                           ParallelContext::CommunicatorSub());
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        invalid_scales == 0,
        "AMG direct interpolation could not form a required sign-separated "
        "scale");

    ParallelFor(nrows+1, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        if (i == nrows) {
            prow[nrows] = pnnz;
        }
    });
    Gpu::streamSynchronize();

    level.P = std::make_unique<matrix_type>(
        partition, std::move(interpolation));
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
void AMG<T>::setup ()
{
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_options.strong_threshold > T(0)
        && m_options.strong_threshold <= T(1),
        "AMG strong_threshold must be in (0,1]");
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

    m_is_setup = false;
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
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            ilev+1 < m_options.max_levels,
            "AMG reached max_levels before the dense coarse threshold");

        build_strength(level);
        split_pmis(level);
        Long const coarse_size =
            level.coarse_partition.numGlobalRows();
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            coarse_size > 0 && coarse_size < fine_size,
            "AMG PMIS did not reduce the global problem size");
        build_interpolation(level);
        build_next_level(level);
    }

    factor_coarse_matrix();
    m_is_setup = true;
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
void AMG<T>::vcycle (int ilev, AlgVector<T>& x,
                     AlgVector<T> const& b)
{
    auto& level = *m_levels[ilev];
    if (ilev+1 == static_cast<int>(m_levels.size())) {
        solve_coarse(x, b);
        return;
    }

    auto const* AMREX_RESTRICT pb = b.data();
    auto const* AMREX_RESTRICT pd = level.l1_norm.data();
    auto* AMREX_RESTRICT px = x.data();
    Long const nrows = x.numLocalRows();
    ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        px[i] = pb[i]/pd[i];
    });

    SpMV(level.work, *level.A, x);
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

    SpMV(level.work, *level.A, x);
    pax = level.work.data();
    ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        px[i] += (pb[i] - pax[i])/pd[i];
    });
    Gpu::streamSynchronize();
}

template <typename T>
void AMG<T>::apply (AlgVector<T>& z, AlgVector<T> const& r)
{
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        m_is_setup, "AMG::setup must be called before AMG::apply");
    AMREX_ALWAYS_ASSERT(z.partition() == m_fine_matrix.partition());
    AMREX_ALWAYS_ASSERT(r.partition() == m_fine_matrix.partition());
    auto& root = *m_levels.front();
    root.rhs.copyAsync(r);
    z.setValAsync(T(0));
    vcycle(0, z, root.rhs);
}

template <typename T>
void AMG<T>::solve (AlgVector<T>& x, AlgVector<T> const& b,
                    T relative_tolerance, T absolute_tolerance,
                    int maximum_iterations)
{
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
}

}

#endif

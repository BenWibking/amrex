#include <AMReX.H>
#include <AMReX_AMG.H>
#include <AMReX_AlgVecUtil.H>
#include <AMReX_GMRES_MV.H>
#include <AMReX_L1JacobiSmoother.H>
#include <AMReX_Math.H>
#include <AMReX_SpGEMM.H>
#include <AMReX_SpMatUtil.H>
#include <AMReX_SpMV.H>

#ifdef AMREX_USE_HYPRE
#include <HYPRE.h>
#include <HYPRE_IJ_mv.h>
#include <HYPRE_parcsr_ls.h>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

using namespace amrex;

namespace amrex {

template <typename T>
struct AMGTestAccess
{
    using amg_type = AMG<T>;
    using level_type = typename amg_type::Level;
    using host_csr_type = typename amg_type::host_csr_type;

    static level_type& prepare_strength (amg_type& amg)
    {
        amg.m_levels.clear();
        auto level = std::make_unique<level_type>();
        level->A = &amg.m_fine_matrix;
        amg.m_levels.push_back(std::move(level));
        amg.initialize_level(*amg.m_levels.front());
        amg.build_strength(*amg.m_levels.front());
        return *amg.m_levels.front();
    }

    static host_csr_type local_strength (amg_type const& amg)
    {
        return amg_type::helper_type::copy_local_global_csr(
            *amg.m_levels.front()->S);
    }

    static host_csr_type local_strength_transpose (amg_type const& amg)
    {
        return amg_type::helper_type::copy_local_global_csr(
            *amg.m_levels.front()->ST);
    }

    static void prepare_pmis (amg_type& amg)
    {
        auto& level = prepare_strength(amg);
        amg.split_pmis(level);
    }

    static Vector<int> global_markers (amg_type const& amg)
    {
        auto const& level = *amg.m_levels.front();
        auto values = amg_type::gather_device_to_host(
            level.cf_marker.data(), level.A->partition());
        return Vector<int>(values.begin(), values.end());
    }

    static Vector<Long> global_coarse_ids (amg_type const& amg)
    {
        auto const& level = *amg.m_levels.front();
        auto values = amg_type::gather_device_to_host(
            level.coarse_id.data(), level.A->partition());
        return Vector<Long>(values.begin(), values.end());
    }

    static AlgPartition const& coarse_partition (amg_type const& amg)
    {
        return amg.m_levels.front()->coarse_partition;
    }

    static void prepare_interpolation (amg_type& amg,
                                       Vector<int> const& global_markers)
    {
        auto& level = prepare_strength(amg);
        Long const begin = level.A->globalRowBegin();
        Long const nlocal = level.A->numLocalRows();
        AMREX_ALWAYS_ASSERT(
            static_cast<Long>(global_markers.size())
            == level.A->numGlobalRows());

        Gpu::PinnedVector<int> local_markers(nlocal);
        Long local_coarse = 0;
        for (Long i = 0; i < nlocal; ++i) {
            local_markers[i] = global_markers[begin+i];
            local_coarse += local_markers[i] == amg_type::coarse;
        }
        level.cf_marker.resize(nlocal);
        if (nlocal > 0) {
            Gpu::copyAsync(Gpu::hostToDevice, local_markers.begin(),
                           local_markers.end(), level.cf_marker.begin());
        }
        level.coarse_partition =
            amg_type::make_coarse_partition(local_coarse);
        Long const coarse_begin =
            level.coarse_partition[ParallelDescriptor::MyProc()];
        Gpu::PinnedVector<Long> local_ids(nlocal, Long(-1));
        Long offset = 0;
        for (Long i = 0; i < nlocal; ++i) {
            if (local_markers[i] == amg_type::coarse) {
                local_ids[i] = coarse_begin+offset;
                ++offset;
            }
        }
        level.coarse_id.resize(nlocal);
        if (nlocal > 0) {
            Gpu::copyAsync(Gpu::hostToDevice, local_ids.begin(),
                           local_ids.end(), level.coarse_id.begin());
        }
        Gpu::streamSynchronize();
        amg.build_interpolation(level);
    }

    static SpMatrix<T> const& interpolation (amg_type const& amg)
    {
        return *amg.m_levels.front()->P;
    }

    static SpMatrix<T> const& restriction (amg_type const& amg)
    {
        return *amg.m_levels.front()->R;
    }

    static constexpr int fine_marker () noexcept
    {
        return amg_type::fine;
    }

    static constexpr int coarse_marker () noexcept
    {
        return amg_type::coarse;
    }
};

}

namespace {

using Entry = std::pair<Long,Real>;
using Entries = Vector<Entry>;

Real
unit_tolerance ()
{
    return (sizeof(Real) == sizeof(float)) ? Real(4.e-5) : Real(8.e-13);
}

Real
solve_tolerance ()
{
    return (sizeof(Real) == sizeof(float)) ? Real(2.e-4) : Real(1.e-9);
}

template <typename F>
SpMatrix<Real>
make_matrix (AlgPartition const& row_partition,
             AlgPartition const& column_partition, F&& make_row)
{
    using host_csr_type = CSR<Real,Gpu::PinnedVector>;
    using csr_type = SpMatrix<Real>::csr_type;

    Long const begin =
        row_partition[ParallelDescriptor::MyProc()];
    Long const end =
        row_partition[ParallelDescriptor::MyProc()+1];
    Long const nlocal = end-begin;
    host_csr_type host;
    host.row_offset.resize(nlocal+1);
    host.row_offset[0] = 0;
    for (Long i = 0; i < nlocal; ++i) {
        Entries entries = make_row(begin+i);
        std::sort(entries.begin(), entries.end(),
                  [] (Entry const& lhs, Entry const& rhs)
                  {
                      return lhs.first < rhs.first;
                  });
        for (std::size_t p = 0; p < entries.size();) {
            Long const column = entries[p].first;
            Real value = Real(0);
            do {
                value += entries[p].second;
                ++p;
            } while (p < entries.size()
                     && entries[p].first == column);
            AMREX_ALWAYS_ASSERT(
                column >= 0
                && column < column_partition.numGlobalRows());
            if (value != Real(0)) {
                host.col_index.push_back(column);
                host.mat.push_back(value);
            }
        }
        host.row_offset[i+1] =
            static_cast<Long>(host.mat.size());
    }
    host.nnz = static_cast<Long>(host.mat.size());
    csr_type device;
    duplicateCSR(Gpu::hostToDevice, device, host);
    Gpu::streamSynchronize();
    return SpMatrix<Real>(
        row_partition, column_partition, std::move(device));
}

template <typename F>
SpMatrix<Real>
make_square_matrix (Long n, F&& make_row)
{
    AlgPartition partition(n);
    return make_matrix(
        partition, partition, std::forward<F>(make_row));
}

Gpu::PinnedVector<Real>
copy_to_host (AlgVector<Real> const& vector)
{
    Gpu::PinnedVector<Real> result(vector.numLocalRows());
    if (!result.empty()) {
#ifdef AMREX_USE_GPU
        Gpu::copyAsync(Gpu::deviceToHost, vector.data(),
                       vector.data()+vector.numLocalRows(),
                       result.begin());
        Gpu::streamSynchronize();
#else
        std::copy(vector.data(),
                  vector.data()+vector.numLocalRows(),
                  result.begin());
#endif
    }
    return result;
}

void
fill_exact (AlgVector<Real>& exact)
{
    auto* x = exact.data();
    Long const begin = exact.globalBegin();
    Long const n = exact.numGlobalRows();
    ParallelFor(exact.numLocalRows(),
                [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        Long const gid = i+begin;
        Real const phase =
            Real(2.0)*Real(3.1415926535897932384626433832795)
            *Real(gid)/Real(n);
        x[i] = Real(1.0)+Real(0.5)*std::sin(phase)
            +Real(0.125)*std::cos(Real(3.0)*phase);
    });
}

Real
true_residual (SpMatrix<Real> const& A,
               AlgVector<Real> const& x,
               AlgVector<Real> const& b)
{
    AlgVector<Real> residual(b.partition());
    SpMV(residual, A, x);
    LinComb(residual, Real(1), b, Real(-1), residual);
    return residual.norm2();
}

Real
solution_error (AlgVector<Real> const& x,
                AlgVector<Real> const& exact)
{
    AlgVector<Real> difference(x.partition());
    difference.copy(x);
    Axpy(difference, Real(-1), exact);
    return difference.norm2();
}

SpMatrix<Real>
make_shifted_2d (int nx, int ny, bool variable,
                 Real x_weight, Real y_weight)
{
    Long const n = static_cast<Long>(nx)*ny;
    auto coefficient = [=] (int i, int j) -> Real
    {
        if (!variable) {
            return Real(1);
        }
        Real const phase_x =
            Real(2.0)*Real(3.1415926535897932384626433832795)
            *Real(i)/Real(nx);
        Real const phase_y =
            Real(2.0)*Real(3.1415926535897932384626433832795)
            *Real(j)/Real(ny);
        return Real(1.25)+Real(0.2)*std::sin(phase_x)
            *std::cos(phase_y);
    };
    return make_square_matrix(
        n, [=] (Long row) -> Entries
        {
            int const i = static_cast<int>(row % nx);
            int const j = static_cast<int>(row / nx);
            Entries entries;
            Real diagonal = Real(1);
            auto add_neighbor =
                [&] (int ni, int nj, Real directional_weight)
                {
                    Real const face_coefficient =
                        Real(0.5)*(coefficient(i,j)
                                   +coefficient(ni,nj));
                    Real const weight =
                        directional_weight*face_coefficient;
                    entries.emplace_back(
                        static_cast<Long>(nj)*nx+ni, -weight);
                    diagonal += weight;
                };
            if (i > 0) {
                add_neighbor(i-1, j, x_weight);
            }
            if (i+1 < nx) {
                add_neighbor(i+1, j, x_weight);
            }
            if (j > 0) {
                add_neighbor(i, j-1, y_weight);
            }
            if (j+1 < ny) {
                add_neighbor(i, j+1, y_weight);
            }
            entries.emplace_back(row, diagonal);
            return entries;
        });
}

SpMatrix<Real>
make_shifted_3d (int nx, int ny, int nz)
{
    Long const n = static_cast<Long>(nx)*ny*nz;
    return make_square_matrix(
        n, [=] (Long row) -> Entries
        {
            int const i = static_cast<int>(row % nx);
            int const j = static_cast<int>((row/nx) % ny);
            int const k = static_cast<int>(row/(nx*ny));
            Entries entries;
            Real diagonal = Real(1);
            auto add_neighbor = [&] (int ni, int nj, int nk)
            {
                entries.emplace_back(
                    (static_cast<Long>(nk)*ny+nj)*nx+ni,
                    Real(-1));
                diagonal += Real(1);
            };
            if (i > 0) {
                add_neighbor(i-1,j,k);
            }
            if (i+1 < nx) {
                add_neighbor(i+1,j,k);
            }
            if (j > 0) {
                add_neighbor(i,j-1,k);
            }
            if (j+1 < ny) {
                add_neighbor(i,j+1,k);
            }
            if (k > 0) {
                add_neighbor(i,j,k-1);
            }
            if (k+1 < nz) {
                add_neighbor(i,j,k+1);
            }
            entries.emplace_back(row, diagonal);
            return entries;
        });
}

void
test_strength_threshold ()
{
    auto A = make_square_matrix(
        4, [] (Long row) -> Entries
        {
            Entries result{{row, Real(4)}};
            if (row == 0) {
                result.emplace_back(1, Real(-1));
                result.emplace_back(2, Real(-0.25));
                result.emplace_back(3, Real(-0.249));
            } else {
                result.emplace_back(0, Real(-1));
            }
            return result;
        });
    AMG<Real> amg(A);
    AMGTestAccess<Real>::prepare_strength(amg);
    auto graph = AMGTestAccess<Real>::local_strength(amg);
    Long const begin = A.globalRowBegin();
    if (begin == 0 && A.numLocalRows() > 0) {
        Vector<Long> columns;
        for (Long p = graph.row_offset[0];
             p < graph.row_offset[1]; ++p)
        {
            columns.push_back(graph.col_index[p]);
            AMREX_ALWAYS_ASSERT(graph.mat[p] != Real(0));
        }
        AMREX_ALWAYS_ASSERT(
            columns == Vector<Long>({1,2}));
    }
}

void
test_distributed_transpose_trailing_empty_row ()
{
#ifdef AMREX_USE_MPI
    int const nprocs = ParallelDescriptor::NProcs();
    if (nprocs < 2) {
        return;
    }

    Vector<Long> offsets(nprocs+1);
    for (int rank = 0; rank <= nprocs; ++rank) {
        offsets[rank] = Long(2)*rank;
    }
    AlgPartition partition(std::move(offsets));
    int const rank = ParallelDescriptor::MyProc();
    Long const begin = partition[rank];
    Long const remote_row = partition[(rank+1)%nprocs];
    auto A = make_matrix(
        partition, partition,
        [=] (Long row) -> Entries
        {
            return (row == begin)
                ? Entries{{remote_row, Real(1)}}
                : Entries{};
        });

    auto AT = amrex::transpose(A, partition);
    auto rows =
        SpGEMMHelper<Real,DefaultAllocator>::copy_local_global_csr(AT);
    Long const expected_column =
        partition[(rank+nprocs-1)%nprocs];
    AMREX_ALWAYS_ASSERT(rows.nnz == 1);
    AMREX_ALWAYS_ASSERT(rows.row_offset.size() == 3);
    AMREX_ALWAYS_ASSERT(rows.row_offset[0] == 0);
    AMREX_ALWAYS_ASSERT(rows.row_offset[1] == 1);
    AMREX_ALWAYS_ASSERT(rows.row_offset[2] == 1);
    AMREX_ALWAYS_ASSERT(rows.col_index[0] == expected_column);
    AMREX_ALWAYS_ASSERT(rows.mat[0] == Real(1));
#endif
}

SpMatrix<Real>
make_periodic_shifted_1d (Long n, Real diagonal)
{
    return make_square_matrix(
        n, [=] (Long row) -> Entries
        {
            return {{(row+n-1)%n, Real(-1)},
                    {row, diagonal},
                    {(row+1)%n, Real(-1)}};
        });
}

void
test_pmis_and_numbering ()
{
    constexpr Long n = 37;
    auto A = make_periodic_shifted_1d(n, Real(3));
    AMG<Real>::Options options;
    options.priority_seed = 19;
    AMG<Real> first(A, options);
    first.setup();
    auto const markers =
        AMGTestAccess<Real>::global_markers(first);
    auto const coarse_ids =
        AMGTestAccess<Real>::global_coarse_ids(first);

    AMG<Real> second(A, options);
    second.setup();
    AMREX_ALWAYS_ASSERT(
        markers == AMGTestAccess<Real>::global_markers(second));

    auto const graph =
        AMGTestAccess<Real>::local_strength(first);
    Long const begin = A.globalRowBegin();
    Long expected_coarse_id = 0;
    for (Long gid = 0; gid < n; ++gid) {
        AMREX_ALWAYS_ASSERT(
            markers[gid] == AMGTestAccess<Real>::fine_marker()
            || markers[gid] == AMGTestAccess<Real>::coarse_marker());
        if (markers[gid] == AMGTestAccess<Real>::coarse_marker()) {
            AMREX_ALWAYS_ASSERT(
                coarse_ids[gid] == expected_coarse_id);
            ++expected_coarse_id;
        } else {
            AMREX_ALWAYS_ASSERT(coarse_ids[gid] == Long(-1));
        }
    }
    AMREX_ALWAYS_ASSERT(
        AMGTestAccess<Real>::coarse_partition(first).numGlobalRows()
        == expected_coarse_id);

    for (Long i = 0; i < A.numLocalRows(); ++i) {
        Long const gid = begin+i;
        if (markers[gid] == AMGTestAccess<Real>::fine_marker()) {
            bool covered = false;
            for (Long p = graph.row_offset[i];
                 p < graph.row_offset[i+1]; ++p)
            {
                covered = covered
                    || markers[graph.col_index[p]]
                       == AMGTestAccess<Real>::coarse_marker();
            }
            AMREX_ALWAYS_ASSERT(covered);
        }
    }
}

void
test_directed_pmis_rounds ()
{
    auto A = make_square_matrix(
        4, [] (Long row) -> Entries
        {
            if (row == 0) {
                return {{0, Real(2)}, {1, Real(-1)}};
            }
            if (row == 1) {
                return {{1, Real(1)}};
            }
            return {{row, Real(2)}, {0, Real(-1)}};
        });
    AMG<Real> amg(A);
    AMGTestAccess<Real>::prepare_pmis(amg);
    auto const markers =
        AMGTestAccess<Real>::global_markers(amg);

    // Point 0 wins the first conflict round. Points 2 and 3 strongly
    // depend on it and become fine. Point 1 remains active because the
    // one-way edge is 0 -> 1, so it becomes coarse in the next round.
    AMREX_ALWAYS_ASSERT(
        markers == Vector<int>(
            {AMGTestAccess<Real>::coarse_marker(),
             AMGTestAccess<Real>::coarse_marker(),
             AMGTestAccess<Real>::fine_marker(),
             AMGTestAccess<Real>::fine_marker()}));
}

void
test_interpolation_restriction_and_galerkin ()
{
    constexpr Long n = 16;
    auto A = make_periodic_shifted_1d(n, Real(2));
    Vector<int> markers(n, AMGTestAccess<Real>::fine_marker());
    for (Long i = 0; i < n; i += 2) {
        markers[i] = AMGTestAccess<Real>::coarse_marker();
    }

    AMG<Real> amg(A);
    AMGTestAccess<Real>::prepare_interpolation(amg, markers);
    auto const& P = AMGTestAccess<Real>::interpolation(amg);
    auto const& R = AMGTestAccess<Real>::restriction(amg);
    auto const coarse_partition =
        AMGTestAccess<Real>::coarse_partition(amg);
    auto rows =
        SpGEMMHelper<Real,DefaultAllocator>::copy_local_global_csr(P);
    Long const begin = A.globalRowBegin();
    for (Long i = 0; i < A.numLocalRows(); ++i) {
        Long const gid = begin+i;
        Long const row_nnz =
            rows.row_offset[i+1]-rows.row_offset[i];
        if (markers[gid] == AMGTestAccess<Real>::coarse_marker()) {
            AMREX_ALWAYS_ASSERT(row_nnz == 1);
            Long const p = rows.row_offset[i];
            AMREX_ALWAYS_ASSERT(rows.col_index[p] == gid/2);
            AMREX_ALWAYS_ASSERT(rows.mat[p] == Real(1));
        } else {
            AMREX_ALWAYS_ASSERT(row_nnz == 2);
            Real row_sum = Real(0);
            for (Long p = rows.row_offset[i];
                 p < rows.row_offset[i+1]; ++p)
            {
                AMREX_ALWAYS_ASSERT(
                    std::abs(rows.mat[p]-Real(0.5))
                    < unit_tolerance());
                row_sum += rows.mat[p];
            }
            AMREX_ALWAYS_ASSERT(
                std::abs(row_sum-Real(1)) < unit_tolerance());
        }
    }

    AlgVector<Real> coarse_ones(coarse_partition);
    AlgVector<Real> fine_ones(A.partition());
    coarse_ones.setVal(Real(1));
    SpMV(fine_ones, P, coarse_ones);
    auto host_fine_ones = copy_to_host(fine_ones);
    for (Real const value : host_fine_ones) {
        AMREX_ALWAYS_ASSERT(
            std::abs(value-Real(1)) < unit_tolerance());
    }

    AlgVector<Real> coarse_vector(coarse_partition);
    AlgVector<Real> fine_vector(A.partition());
    auto* pc = coarse_vector.data();
    auto* pf = fine_vector.data();
    Long const coarse_begin = coarse_vector.globalBegin();
    Long const fine_begin = fine_vector.globalBegin();
    ParallelFor(coarse_vector.numLocalRows(),
                [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        pc[i] = Real(0.5)+Real(0.125)*Real(i+coarse_begin);
    });
    ParallelFor(fine_vector.numLocalRows(),
                [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        pf[i] = Real(1.0)-Real(0.0625)*Real(i+fine_begin);
    });
    AlgVector<Real> restricted(coarse_partition);
    AlgVector<Real> prolonged(A.partition());
    SpMV(restricted, R, fine_vector);
    SpMV(prolonged, P, coarse_vector);
    AMREX_ALWAYS_ASSERT(
        std::abs(Dot(coarse_vector, restricted)
                 -Dot(prolonged, fine_vector))
        < Real(20)*unit_tolerance());

    auto AP = SpGEMM(A, P, coarse_partition);
    auto Ac = SpGEMM(R, AP, coarse_partition);
    AlgVector<Real> acv(coarse_partition);
    AlgVector<Real> pv(A.partition());
    AlgVector<Real> apv(A.partition());
    AlgVector<Real> rapv(coarse_partition);
    SpMV(acv, Ac, coarse_vector);
    SpMV(pv, P, coarse_vector);
    SpMV(apv, A, pv);
    SpMV(rapv, R, apv);
    Axpy(acv, Real(-1), rapv);
    AMREX_ALWAYS_ASSERT(acv.norminf() < Real(20)*unit_tolerance());
}

void
test_l1_jacobi ()
{
    auto A = make_square_matrix(
        3, [] (Long row) -> Entries
        {
            Entries result{{row, Real(2)}};
            if (row > 0) {
                result.emplace_back(row-1, Real(-1));
            }
            if (row+1 < 3) {
                result.emplace_back(row+1, Real(-1));
            }
            return result;
        });
    L1JacobiSmoother<Real> smoother(A);
    AlgVector<Real> x(A.partition());
    AlgVector<Real> b(A.partition());
    AlgVector<Real> work(A.partition());
    auto* pb = b.data();
    Long const begin = b.globalBegin();
    ParallelFor(b.numLocalRows(),
                [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        pb[i] = Real(i+begin+1);
    });
    x.setVal(Real(0));
    smoother.sweep(x, b, work, true);
    auto first = copy_to_host(x);
    auto denominator = copy_to_host(smoother.denominator());
    for (Long i = 0; i < x.numLocalRows(); ++i) {
        Real const expected =
            Real(i+begin+1)/denominator[i];
        AMREX_ALWAYS_ASSERT(
            std::abs(first[i]-expected) < unit_tolerance());
    }

    SpMV(work, A, x);
    auto ax = copy_to_host(work);
    smoother.sweep(x, b, work, false);
    auto second = copy_to_host(x);
    for (Long i = 0; i < x.numLocalRows(); ++i) {
        Real const expected = first[i]
            +(Real(i+begin+1)-ax[i])/denominator[i];
        AMREX_ALWAYS_ASSERT(
            std::abs(second[i]-expected) < unit_tolerance());
    }
}

void
test_all_coarse_sizes ()
{
    for (Long n = 1; n <= 9; ++n) {
        auto A = make_square_matrix(
            n, [] (Long row) -> Entries
            {
                return {{row, Real(2)+Real(0.125)*Real(row)}};
            });
        AlgVector<Real> exact(A.partition());
        AlgVector<Real> b(A.partition());
        AlgVector<Real> solution(A.partition());
        fill_exact(exact);
        SpMV(b, A, exact);
        AMG<Real> amg(A);
        amg.setup();
        AMREX_ALWAYS_ASSERT(amg.numLevels() == 1);
        amg.apply(solution, b);
        AMREX_ALWAYS_ASSERT(
            solution_error(solution, exact)
            < Real(20)*unit_tolerance());
    }
}

#ifdef AMREX_USE_HYPRE
AlgVector<Real>
solve_with_boomeramg (SpMatrix<Real> const& A,
                      AlgVector<Real> const& b,
                      Real tolerance, int maximum_iterations,
                      int& iterations)
{
    auto const comm = ParallelContext::CommunicatorSub();
    auto const& partition = A.partition();
    Long const begin = A.globalRowBegin();
    Long const end = A.globalRowEnd();
    Long const nlocal = A.numLocalRows();
    auto rows =
        SpGEMMHelper<Real,DefaultAllocator>::copy_local_global_csr(A);
    AMREX_ALWAYS_ASSERT(nlocal <= std::numeric_limits<HYPRE_Int>::max());
    AMREX_ALWAYS_ASSERT(rows.nnz <= std::numeric_limits<HYPRE_Int>::max());

    HYPRE_IJMatrix ij_A = nullptr;
    HYPRE_IJVector ij_b = nullptr;
    HYPRE_IJVector ij_x = nullptr;
    HYPRE_Solver solver = nullptr;
    auto const ilower = static_cast<HYPRE_BigInt>(begin);
    auto const iupper = static_cast<HYPRE_BigInt>(end-1);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJMatrixCreate(
            comm, ilower, iupper, ilower, iupper, &ij_A) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJMatrixSetObjectType(ij_A, HYPRE_PARCSR) == 0);
    AMREX_ALWAYS_ASSERT(HYPRE_IJMatrixInitialize(ij_A) == 0);

    Gpu::DeviceVector<Long> device_offsets(rows.row_offset.size());
    Gpu::DeviceVector<Long> device_columns(rows.col_index.size());
    Gpu::DeviceVector<Real> device_values(rows.mat.size());
    if (!rows.row_offset.empty()) {
        Gpu::copyAsync(Gpu::hostToDevice, rows.row_offset.begin(),
                       rows.row_offset.end(), device_offsets.begin());
    }
    if (rows.nnz > 0) {
        Gpu::copyAsync(Gpu::hostToDevice, rows.col_index.begin(),
                       rows.col_index.end(), device_columns.begin());
        Gpu::copyAsync(Gpu::hostToDevice, rows.mat.begin(),
                       rows.mat.end(), device_values.begin());
    }

    Gpu::DeviceVector<HYPRE_Int> row_nnz(nlocal);
    Gpu::DeviceVector<HYPRE_BigInt> row_ids(nlocal);
    Gpu::DeviceVector<HYPRE_BigInt> columns(rows.nnz);
    Gpu::DeviceVector<HYPRE_Complex> values(rows.nnz);
    Gpu::DeviceVector<HYPRE_Complex> rhs_values(nlocal);
    auto const* offsets = device_offsets.data();
    auto const* source_columns = device_columns.data();
    auto const* source_values = device_values.data();
    auto const* rhs = b.data();
    auto* pncols = row_nnz.data();
    auto* prows = row_ids.data();
    auto* pcols = columns.data();
    auto* pvalues = values.data();
    auto* prhs = rhs_values.data();
    ParallelFor(nlocal,
                [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        pncols[i] = static_cast<HYPRE_Int>(
            offsets[i+1]-offsets[i]);
        prows[i] = static_cast<HYPRE_BigInt>(begin+i);
        prhs[i] = static_cast<HYPRE_Complex>(rhs[i]);
    });
    ParallelFor(rows.nnz,
                [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        pcols[i] = static_cast<HYPRE_BigInt>(source_columns[i]);
        pvalues[i] = static_cast<HYPRE_Complex>(source_values[i]);
    });
    Gpu::streamSynchronize();
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJMatrixSetValues(
            ij_A, static_cast<HYPRE_Int>(nlocal), row_nnz.data(),
            row_ids.data(), columns.data(), values.data()) == 0);
    AMREX_ALWAYS_ASSERT(HYPRE_IJMatrixAssemble(ij_A) == 0);

    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorCreate(comm, ilower, iupper, &ij_b) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorSetObjectType(ij_b, HYPRE_PARCSR) == 0);
    AMREX_ALWAYS_ASSERT(HYPRE_IJVectorInitialize(ij_b) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorSetValues(
            ij_b, static_cast<HYPRE_Int>(nlocal),
            row_ids.data(), rhs_values.data()) == 0);
    AMREX_ALWAYS_ASSERT(HYPRE_IJVectorAssemble(ij_b) == 0);

    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorCreate(comm, ilower, iupper, &ij_x) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorSetObjectType(ij_x, HYPRE_PARCSR) == 0);
    AMREX_ALWAYS_ASSERT(HYPRE_IJVectorInitialize(ij_x) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorSetConstantValues(ij_x, HYPRE_Complex(0)) == 0);
    AMREX_ALWAYS_ASSERT(HYPRE_IJVectorAssemble(ij_x) == 0);

    HYPRE_ParCSRMatrix par_A = nullptr;
    HYPRE_ParVector par_b = nullptr;
    HYPRE_ParVector par_x = nullptr;
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJMatrixGetObject(
            ij_A, reinterpret_cast<void**>(&par_A)) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorGetObject(
            ij_b, reinterpret_cast<void**>(&par_b)) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorGetObject(
            ij_x, reinterpret_cast<void**>(&par_x)) == 0);

    AMREX_ALWAYS_ASSERT(HYPRE_BoomerAMGCreate(&solver) == 0);
    HYPRE_BoomerAMGSetStrongThreshold(solver, HYPRE_Real(0.25));
    HYPRE_BoomerAMGSetCoarsenType(solver, 8);
    HYPRE_BoomerAMGSetInterpType(solver, 3);
    HYPRE_BoomerAMGSetPMaxElmts(solver, 0);
    HYPRE_BoomerAMGSetTruncFactor(solver, HYPRE_Real(0));
    HYPRE_BoomerAMGSetAggNumLevels(solver, 0);
    HYPRE_BoomerAMGSetCycleType(solver, 1);
    HYPRE_BoomerAMGSetRelaxOrder(solver, 0);
    HYPRE_BoomerAMGSetCycleRelaxType(solver, 18, 1);
    HYPRE_BoomerAMGSetCycleRelaxType(solver, 18, 2);
    HYPRE_BoomerAMGSetCycleRelaxType(solver, 9, 3);
    HYPRE_BoomerAMGSetCycleNumSweeps(solver, 1, 1);
    HYPRE_BoomerAMGSetCycleNumSweeps(solver, 1, 2);
    HYPRE_BoomerAMGSetCycleNumSweeps(solver, 1, 3);
    HYPRE_BoomerAMGSetMaxCoarseSize(solver, 9);
    HYPRE_BoomerAMGSetMaxLevels(solver, 25);
    HYPRE_BoomerAMGSetKeepTranspose(solver, 1);
    HYPRE_BoomerAMGSetTol(
        solver, static_cast<HYPRE_Real>(tolerance));
    HYPRE_BoomerAMGSetMaxIter(solver, maximum_iterations);
    HYPRE_BoomerAMGSetPrintLevel(solver, 0);
    HYPRE_BoomerAMGSetLogging(solver, 1);
    AMREX_ALWAYS_ASSERT(
        HYPRE_BoomerAMGSetup(solver, par_A, par_b, par_x) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_BoomerAMGSolve(solver, par_A, par_b, par_x) == 0);
    HYPRE_Int hypre_iterations = 0;
    HYPRE_BoomerAMGGetNumIterations(solver, &hypre_iterations);
    iterations = static_cast<int>(hypre_iterations);

    Gpu::DeviceVector<HYPRE_Complex> solution_values(nlocal);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorGetValues(
            ij_x, static_cast<HYPRE_Int>(nlocal),
            row_ids.data(), solution_values.data()) == 0);
    Gpu::hypreSynchronize();
    AlgVector<Real> result(partition);
    auto const* phypre = solution_values.data();
    auto* presult = result.data();
    ParallelFor(nlocal,
                [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        presult[i] = static_cast<Real>(phypre[i]);
    });
    Gpu::streamSynchronize();

    HYPRE_BoomerAMGDestroy(solver);
    HYPRE_IJVectorDestroy(ij_x);
    HYPRE_IJVectorDestroy(ij_b);
    HYPRE_IJMatrixDestroy(ij_A);
    return result;
}
#endif

void
test_manufactured_problem (std::string const& name,
                           SpMatrix<Real>& A)
{
    AlgVector<Real> exact(A.partition());
    AlgVector<Real> b(A.partition());
    fill_exact(exact);
    SpMV(b, A, exact);
    Real const bnorm = b.norm2();
    Real const rtol = solve_tolerance();
    Real const internal_rtol = Real(0.1)*rtol;
    Real const target = rtol*bnorm;

    AMG<Real> amg(A);
    amg.setup();
    AMREX_ALWAYS_ASSERT(amg.numLevels() > 1);

    AlgVector<Real> one_cycle(A.partition());
    amg.apply(one_cycle, b);
    AMREX_ALWAYS_ASSERT(
        true_residual(A, one_cycle, b) < bnorm);

    AlgVector<Real> stationary(A.partition());
    stationary.setVal(Real(0));
    amg.solve(stationary, b, internal_rtol, Real(0), 150);
    Real const stationary_residual =
        true_residual(A, stationary, b);
    Real const stationary_error =
        solution_error(stationary, exact);
    AMREX_ALWAYS_ASSERT(stationary_residual <= target);

    AlgVector<Real> gmres_solution(A.partition());
    gmres_solution.setVal(Real(0));
    GMRES_MV<Real> gmres(&A);
    gmres.setPrecond(
        [&amg] (AlgVector<Real>& lhs,
                AlgVector<Real> const& rhs)
        {
            amg.apply(lhs, rhs);
        });
    gmres.solve(gmres_solution, b, rtol, Real(0));
    Real const gmres_residual =
        true_residual(A, gmres_solution, b);
    Real const gmres_error =
        solution_error(gmres_solution, exact);
    AMREX_ALWAYS_ASSERT(gmres_residual <= target);

    auto const& diagnostics = amg.diagnostics();
    AMREX_ALWAYS_ASSERT(
        diagnostics.levels.size()
        == static_cast<std::size_t>(amg.numLevels()));
    AMREX_ALWAYS_ASSERT(diagnostics.grid_complexity >= 1.0);
    AMREX_ALWAYS_ASSERT(diagnostics.operator_complexity >= 1.0);
    AMREX_ALWAYS_ASSERT(diagnostics.interpolation_complexity > 0.0);
    AMREX_ALWAYS_ASSERT(diagnostics.setup_seconds >= 0.0);
    AMREX_ALWAYS_ASSERT(diagnostics.last_apply_seconds >= 0.0);
    AMREX_ALWAYS_ASSERT(diagnostics.last_solve_seconds >= 0.0);
    AMREX_ALWAYS_ASSERT(diagnostics.hierarchy_storage_bytes > 0);

    amrex::Print()
        << name << ": stationary iterations=" << amg.lastIterations()
        << ", stationary residual=" << stationary_residual
        << ", stationary exact error=" << stationary_error
        << ", GMRES residual=" << gmres_residual
        << ", GMRES exact error=" << gmres_error << '\n';
    amg.printDiagnostics();

#ifdef AMREX_USE_HYPRE
    int hypre_iterations = 0;
    auto hypre_solution =
        solve_with_boomeramg(
            A, b, internal_rtol, 150, hypre_iterations);
    Real const hypre_residual =
        true_residual(A, hypre_solution, b);
    Real const hypre_error =
        solution_error(hypre_solution, exact);
    AlgVector<Real> difference(A.partition());
    difference.copy(stationary);
    Axpy(difference, Real(-1), hypre_solution);
    Real const solution_difference = difference.norm2();
    Real const agreement_limit =
        rtol*hypre_solution.norm2();
    AMREX_ALWAYS_ASSERT(hypre_residual <= target);
    AMREX_ALWAYS_ASSERT(
        solution_difference <= agreement_limit);
    amrex::Print()
        << name << " BoomerAMG: iterations=" << hypre_iterations
        << ", true residual=" << hypre_residual
        << ", exact error=" << hypre_error
        << ", AMReX/HYPRE difference=" << solution_difference
        << ", agreement limit=" << agreement_limit << '\n';
#endif
}

}

int
main (int argc, char* argv[])
{
    amrex::Initialize(argc, argv);

    test_strength_threshold();
    test_distributed_transpose_trailing_empty_row();
    test_pmis_and_numbering();
    test_directed_pmis_rounds();
    test_interpolation_restriction_and_galerkin();
    test_l1_jacobi();
    test_all_coarse_sizes();

    auto shifted_2d =
        make_shifted_2d(10, 9, false, Real(1), Real(1));
    test_manufactured_problem("2-D shifted Poisson", shifted_2d);

    auto shifted_3d = make_shifted_3d(5, 5, 4);
    test_manufactured_problem("3-D shifted Poisson", shifted_3d);

    auto variable =
        make_shifted_2d(10, 8, true, Real(1), Real(1));
    test_manufactured_problem(
        "2-D variable-coefficient diffusion", variable);

    auto anisotropic =
        make_shifted_2d(10, 8, false, Real(4), Real(1));
    test_manufactured_problem(
        "2-D anisotropic diffusion", anisotropic);

    amrex::Print()
        << "AMG strength, distributed transpose, PMIS, numbering, "
        << "interpolation, restriction, Galerkin, smoother, coarse-solve, "
        << "manufactured-solution, and diagnostics tests passed\n";
    amrex::Finalize();
}

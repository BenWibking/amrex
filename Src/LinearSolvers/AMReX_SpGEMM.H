#ifndef AMREX_SPGEMM_H_
#define AMREX_SPGEMM_H_
#include <AMReX_Config.H>

#include <AMReX_Gpu.H>
#include <AMReX_ParallelContext.H>
#include <AMReX_Reduce.H>
#include <AMReX_Scan.H>
#include <AMReX_SpMatrix.H>

#include <algorithm>
#include <limits>
#include <utility>

namespace amrex {

/**
 * \file AMReX_SpGEMM.H
 *
 * Correctness-first sparse matrix-matrix multiplication for distributed
 * AMReX CSR matrices.  The numerical product is formed with portable AMReX
 * device kernels.  MPI builds currently replicate the rows of the right
 * operand during setup; this deliberately simple communication path is
 * intended to be replaced by sparse row import without changing the public
 * interface.
 */

template <typename T, template <typename> class Allocator>
struct SpGEMMHelper
{
    using matrix_type = SpMatrix<T,Allocator>;
    using csr_type = typename matrix_type::csr_type;
    using host_csr_type = CSR<T,Gpu::PinnedVector>;

    template <typename U>
    using container_type = typename matrix_type::template container_type<U>;

    static host_csr_type copy_local_global_csr (matrix_type const& A)
    {
        host_csr_type hlocal;
        host_csr_type hremote;

        copy_to_host(hlocal, A.m_csr);
#ifdef AMREX_USE_MPI
        if (A.m_split && A.m_csr_remote.nnz > 0) {
            copy_to_host(hremote, A.m_csr_remote);
        }
#endif

        Long const nrows = A.numLocalRows();
        host_csr_type result;
        result.row_offset.resize(nrows+1);
        result.row_offset[0] = 0;

        Gpu::PinnedVector<Long> row_to_remote;
#ifdef AMREX_USE_MPI
        if (A.m_split) {
            row_to_remote.resize(A.m_ri_ltor.size());
#ifdef AMREX_USE_GPU
            Gpu::copyAsync(Gpu::deviceToHost,
                           A.m_ri_ltor.begin(), A.m_ri_ltor.end(),
                           row_to_remote.begin());
            Gpu::streamSynchronize();
#else
            std::copy(A.m_ri_ltor.begin(), A.m_ri_ltor.end(),
                      row_to_remote.begin());
#endif
        }
#endif

        Vector<std::pair<Long,T>> row_entries;
        for (Long i = 0; i < nrows; ++i) {
            row_entries.clear();
            auto const local_begin = hlocal.row_offset[i];
            auto const local_end = hlocal.row_offset[i+1];
            row_entries.reserve(static_cast<std::size_t>(local_end-local_begin));
            for (Long p = local_begin; p < local_end; ++p) {
                Long col = hlocal.col_index[p];
                if (A.m_split) {
                    col += A.m_col_begin;
                }
                row_entries.emplace_back(col, hlocal.mat[p]);
            }

#ifdef AMREX_USE_MPI
            if (A.m_split && !row_to_remote.empty() && row_to_remote[i] >= 0) {
                auto const rr = row_to_remote[i];
                for (Long p = hremote.row_offset[rr];
                     p < hremote.row_offset[rr+1]; ++p)
                {
                    row_entries.emplace_back(A.m_remote_cols_v[hremote.col_index[p]],
                                             hremote.mat[p]);
                }
            }
#endif

            std::sort(row_entries.begin(), row_entries.end(),
                      [] (auto const& lhs, auto const& rhs) {
                          return lhs.first < rhs.first;
                      });
            for (auto const& [col,val] : row_entries) {
                result.col_index.push_back(col);
                result.mat.push_back(val);
            }
            result.row_offset[i+1] = static_cast<Long>(result.mat.size());
        }
        result.nnz = static_cast<Long>(result.mat.size());
        return result;
    }

    static host_csr_type gather_global_rows (matrix_type const& B)
    {
        auto local = copy_local_global_csr(B);

#ifndef AMREX_USE_MPI
        return local;
#else
        auto const& partition = B.partition().dataVector();
        int const nprocs = ParallelContext::NProcsSub();
        int const myproc = ParallelContext::MyProcSub();
        auto const comm = ParallelContext::CommunicatorSub();
        auto const mpi_long = ParallelDescriptor::Mpi_typemap<Long>::type();
        auto const mpi_t = ParallelDescriptor::Mpi_typemap<T>::type();

        AMREX_ALWAYS_ASSERT(static_cast<int>(partition.size()) == nprocs+1);

        Vector<int> row_counts(nprocs);
        Vector<int> row_displs(nprocs);
        for (int p = 0; p < nprocs; ++p) {
            auto const nr = partition[p+1] - partition[p];
            AMREX_ALWAYS_ASSERT(nr <= std::numeric_limits<int>::max());
            AMREX_ALWAYS_ASSERT(partition[p] <= std::numeric_limits<int>::max());
            row_counts[p] = static_cast<int>(nr);
            row_displs[p] = static_cast<int>(partition[p]);
        }

        Gpu::PinnedVector<Long> local_row_nnz(B.numLocalRows());
        for (Long i = 0; i < B.numLocalRows(); ++i) {
            local_row_nnz[i] = local.row_offset[i+1] - local.row_offset[i];
        }

        host_csr_type global;
        Long const global_nrows = B.numGlobalRows();
        Gpu::PinnedVector<Long> global_row_nnz(global_nrows);
        BL_MPI_REQUIRE(MPI_Allgatherv(local_row_nnz.data(), row_counts[myproc],
                                     mpi_long, global_row_nnz.data(),
                                     row_counts.data(), row_displs.data(),
                                     mpi_long, comm));

        Vector<Long> nnz_counts_long(nprocs);
        Long local_nnz = local.nnz;
        BL_MPI_REQUIRE(MPI_Allgather(&local_nnz, 1, mpi_long,
                                    nnz_counts_long.data(), 1, mpi_long, comm));

        Vector<int> nnz_counts(nprocs);
        Vector<int> nnz_displs(nprocs);
        Long global_nnz = 0;
        for (int p = 0; p < nprocs; ++p) {
            AMREX_ALWAYS_ASSERT(nnz_counts_long[p] <=
                                std::numeric_limits<int>::max());
            AMREX_ALWAYS_ASSERT(global_nnz <= std::numeric_limits<int>::max());
            nnz_counts[p] = static_cast<int>(nnz_counts_long[p]);
            nnz_displs[p] = static_cast<int>(global_nnz);
            global_nnz += nnz_counts_long[p];
        }

        global.resize(global_nrows, global_nnz);
        BL_MPI_REQUIRE(MPI_Allgatherv(local.mat.data(), nnz_counts[myproc],
                                     mpi_t, global.mat.data(),
                                     nnz_counts.data(), nnz_displs.data(),
                                     mpi_t, comm));
        BL_MPI_REQUIRE(MPI_Allgatherv(local.col_index.data(),
                                     nnz_counts[myproc], mpi_long,
                                     global.col_index.data(),
                                     nnz_counts.data(), nnz_displs.data(),
                                     mpi_long, comm));

        global.row_offset[0] = 0;
        for (Long i = 0; i < global_nrows; ++i) {
            global.row_offset[i+1] = global.row_offset[i] + global_row_nnz[i];
        }
        AMREX_ALWAYS_ASSERT(global.row_offset[global_nrows] == global_nnz);
        return global;
#endif
    }

    static csr_type multiply (host_csr_type const& hA,
                              host_csr_type const& hB,
                              Long ncols)
    {
        csr_type A;
        csr_type B;
        copy_to_device(A, hA);
        copy_to_device(B, hB);

        Long const nrows = A.nrows();
        if (nrows == 0 || A.nnz == 0 || B.nnz == 0) {
            csr_type empty;
            empty.resize(nrows, 0);
            ParallelFor(nrows+1, [p=empty.row_offset.data()]
                         AMREX_GPU_DEVICE (Long i) noexcept { p[i] = 0; });
            Gpu::streamSynchronize();
            return empty;
        }

        auto const* AMREX_RESTRICT arow = A.row_offset.data();
        auto const* AMREX_RESTRICT acol = A.col_index.data();
        auto const* AMREX_RESTRICT brow = B.row_offset.data();
        Long const brows = B.nrows();

        container_type<Long> product_counts(nrows);
        auto* AMREX_RESTRICT pcounts = product_counts.data();
        auto invalid = Reduce::Sum<int>(
            nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept -> int
            {
                Long count = 0;
                int bad = 0;
                for (Long ap = arow[i]; ap < arow[i+1]; ++ap) {
                    Long const k = acol[ap];
                    if (k < 0 || k >= brows) {
                        bad = 1;
                    } else {
                        count += brow[k+1] - brow[k];
                    }
                }
                pcounts[i] = count;
                return bad;
            });
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            invalid == 0, "SpGEMM: left column index is outside right row range");

        csr_type products;
        products.row_offset.resize(nrows+1);
        auto* AMREX_RESTRICT product_row = products.row_offset.data();
        Long const num_products = Scan::PrefixSum<Long>(
            nrows,
            [=] AMREX_GPU_DEVICE (Long i) noexcept -> Long {
                return pcounts[i];
            },
            [=] AMREX_GPU_DEVICE (Long i, Long x) noexcept {
                product_row[i] = x;
            },
            Scan::Type::exclusive, Scan::retSum);
        products.nnz = num_products;
        products.mat.resize(num_products);
        products.col_index.resize(num_products);
        auto* AMREX_RESTRICT product_val = products.mat.data();
        auto* AMREX_RESTRICT product_col = products.col_index.data();
        auto const* AMREX_RESTRICT aval = A.mat.data();
        auto const* AMREX_RESTRICT bval = B.mat.data();
        auto const* AMREX_RESTRICT bcol = B.col_index.data();
        ParallelFor(nrows+1, [=] AMREX_GPU_DEVICE (Long i) noexcept
        {
            if (i == nrows) {
                product_row[nrows] = num_products;
                return;
            }
            Long out = product_row[i];
            for (Long ap = arow[i]; ap < arow[i+1]; ++ap) {
                Long const k = acol[ap];
                T const a = aval[ap];
                for (Long bp = brow[k]; bp < brow[k+1]; ++bp) {
                    product_col[out] = bcol[bp];
                    product_val[out] = a*bval[bp];
                    ++out;
                }
            }
        });

        products.sort();

        container_type<Long> unique_counts(nrows);
        auto* AMREX_RESTRICT ucounts = unique_counts.data();
        ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
        {
            Long count = 0;
            Long previous = -1;
            for (Long p = product_row[i]; p < product_row[i+1]; ++p) {
                Long const col = product_col[p];
                if (p == product_row[i] || col != previous) {
                    ++count;
                    previous = col;
                }
            }
            ucounts[i] = count;
        });

        csr_type C;
        C.row_offset.resize(nrows+1);
        auto* AMREX_RESTRICT crow = C.row_offset.data();
        Long const cnnz = Scan::PrefixSum<Long>(
            nrows,
            [=] AMREX_GPU_DEVICE (Long i) noexcept -> Long {
                return ucounts[i];
            },
            [=] AMREX_GPU_DEVICE (Long i, Long x) noexcept {
                crow[i] = x;
            },
            Scan::Type::exclusive, Scan::retSum);
        C.nnz = cnnz;
        C.mat.resize(cnnz);
        C.col_index.resize(cnnz);
        auto* AMREX_RESTRICT cval = C.mat.data();
        auto* AMREX_RESTRICT ccol = C.col_index.data();

        ParallelFor(nrows+1, [=] AMREX_GPU_DEVICE (Long i) noexcept
        {
            if (i == nrows) {
                crow[nrows] = cnnz;
                return;
            }
            Long out = crow[i];
            Long p = product_row[i];
            while (p < product_row[i+1]) {
                Long const col = product_col[p];
                T value = T(0);
                do {
                    value += product_val[p];
                    ++p;
                } while (p < product_row[i+1] && product_col[p] == col);
                AMREX_ASSERT(col >= 0 && col < ncols);
                ccol[out] = col;
                cval[out] = value;
                ++out;
            }
        });
        Gpu::streamSynchronize();
        return C;
    }

private:
    template <template<typename> class V>
    static void copy_to_host (host_csr_type& dst, CSR<T,V> const& src)
    {
        dst.resize(src.nrows(), src.nnz);
#ifdef AMREX_USE_GPU
        Gpu::copyAsync(Gpu::deviceToHost, src.mat.begin(), src.mat.end(),
                       dst.mat.begin());
        Gpu::copyAsync(Gpu::deviceToHost, src.col_index.begin(),
                       src.col_index.end(), dst.col_index.begin());
        Gpu::copyAsync(Gpu::deviceToHost, src.row_offset.begin(),
                       src.row_offset.end(), dst.row_offset.begin());
        Gpu::streamSynchronize();
#else
        std::copy(src.mat.begin(), src.mat.end(), dst.mat.begin());
        std::copy(src.col_index.begin(), src.col_index.end(),
                  dst.col_index.begin());
        std::copy(src.row_offset.begin(), src.row_offset.end(),
                  dst.row_offset.begin());
#endif
    }

    static void copy_to_device (csr_type& dst, host_csr_type const& src)
    {
        dst.resize(src.nrows(), src.nnz);
#ifdef AMREX_USE_GPU
        Gpu::copyAsync(Gpu::hostToDevice, src.mat.begin(), src.mat.end(),
                       dst.mat.begin());
        Gpu::copyAsync(Gpu::hostToDevice, src.col_index.begin(),
                       src.col_index.end(), dst.col_index.begin());
        Gpu::copyAsync(Gpu::hostToDevice, src.row_offset.begin(),
                       src.row_offset.end(), dst.row_offset.begin());
        Gpu::streamSynchronize();
#else
        std::copy(src.mat.begin(), src.mat.end(), dst.mat.begin());
        std::copy(src.col_index.begin(), src.col_index.end(),
                  dst.col_index.begin());
        std::copy(src.row_offset.begin(), src.row_offset.end(),
                  dst.row_offset.begin());
#endif
    }
};

/**
 * \brief Form the distributed sparse product \f$C=AB\f$.
 *
 * \param A Left operand. Its row partition becomes the product row
 *          partition.
 * \param B Right operand.
 * \param col_partition Column partition of the product.
 */
template <typename T, template <typename> class Allocator>
SpMatrix<T,Allocator>
SpGEMM (SpMatrix<T,Allocator> const& A,
        SpMatrix<T,Allocator> const& B,
        AlgPartition const& col_partition)
{
    auto hA = SpGEMMHelper<T,Allocator>::copy_local_global_csr(A);
    auto hB = SpGEMMHelper<T,Allocator>::gather_global_rows(B);
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        hB.nrows() == B.numGlobalRows(),
        "SpGEMM: inconsistent right-operand row storage");
    for (auto const col : hA.col_index) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            col >= 0 && col < hB.nrows(),
            "SpGEMM: left column index is outside right row range");
    }
    for (auto const col : hB.col_index) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            col >= 0 && col < col_partition.numGlobalRows(),
            "SpGEMM: right column index is outside product column range");
    }
    auto C = SpGEMMHelper<T,Allocator>::multiply(
        hA, hB, col_partition.numGlobalRows());
    return SpMatrix<T,Allocator>(A.partition(), std::move(C));
}

}

#endif

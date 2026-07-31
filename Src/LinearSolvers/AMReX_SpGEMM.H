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
#include <unordered_map>
#include <utility>

namespace amrex {

/**
 * \file AMReX_SpGEMM.H
 *
 * Sparse matrix-matrix multiplication for distributed AMReX CSR matrices.
 * The numerical product is formed with portable AMReX device kernels. MPI
 * builds import only right-operand rows referenced by locally owned rows of
 * the left operand.
 */

/**
 * \brief Internal host-staging and device-product operations used by
 * \ref SpGEMM.
 *
 * \tparam T Floating-point matrix scalar type.
 * \tparam Allocator Storage allocator used by the distributed matrices.
 */
template <typename T, template <typename> class Allocator>
struct SpGEMMHelper
{
    //! Distributed sparse-matrix type operated on by this helper.
    using matrix_type = SpMatrix<T,Allocator>;
    //! Device-accessible CSR storage used by \ref matrix_type.
    using csr_type = typename matrix_type::csr_type;
    //! CSR storage backed by pinned host vectors.
    using host_csr_type = CSR<T,Gpu::PinnedVector>;

    //! Matrix-allocator vector type for temporary values of type \p U.
    template <typename U>
    using container_type = typename matrix_type::template container_type<U>;

    /**
     * \brief Reconstruct the locally owned rows of \p A with global column
     * indices.
     *
     * Split local and remote CSR blocks are merged and sorted into canonical
     * pinned-host CSR storage.
     *
     * \param A Distributed source matrix.
     * \return Local rows of \p A in pinned host memory.
     */
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
            for (std::size_t p = 0; p < row_entries.size();) {
                Long const col = row_entries[p].first;
                T value = T(0);
                do {
                    value += row_entries[p].second;
                    ++p;
                } while (p < row_entries.size()
                         && row_entries[p].first == col);
                if (value != T(0)) {
                    result.col_index.push_back(col);
                    result.mat.push_back(value);
                }
            }
            result.row_offset[i+1] = static_cast<Long>(result.mat.size());
        }
        result.nnz = static_cast<Long>(result.mat.size());
        return result;
    }

    /**
     * \brief Replicate all global rows of \p B in pinned host memory.
     *
     * In MPI builds, row lengths, values, and global column indices are
     * gathered onto every rank. This is the correctness-first setup path.
     *
     * \param B Distributed right operand.
     * \return Complete global CSR representation of \p B.
     */
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

    /**
     * \brief Import the rows of \p B referenced by the local rows of \p A.
     *
     * On return, columns of \p A are remapped to compact row indices in the
     * returned right operand. MPI ranks exchange row requests, row lengths,
     * column indices, and values only for the required off-rank rows.
     *
     * \param A Canonical local rows of the left operand. Column indices are
     *          remapped in place.
     * \param B Distributed right operand.
     * \return Compact right-operand rows required by \p A.
     */
    static host_csr_type import_required_rows (host_csr_type& A,
                                               matrix_type const& B)
    {
        auto local = copy_local_global_csr(B);
        auto const& partition = B.partition().dataVector();
        Long const global_nrows = B.numGlobalRows();
        int const myproc = ParallelContext::MyProcSub();
        int const nprocs = ParallelContext::NProcsSub();
        Long const local_begin = partition[myproc];
        Long const local_end = partition[myproc+1];

        Vector<Long> needed(A.col_index.begin(), A.col_index.end());
        std::sort(needed.begin(), needed.end());
        needed.erase(std::unique(needed.begin(), needed.end()), needed.end());
        for (Long const row : needed) {
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                row >= 0 && row < global_nrows,
                "SpGEMM: left column index is outside right row range");
        }

        struct RowSlice {
            Long offset = 0;
            Long length = 0;
        };
        std::unordered_map<Long,RowSlice> remote_rows;
        Gpu::PinnedVector<Long> received_columns;
        Gpu::PinnedVector<T> received_values;

#ifdef AMREX_USE_MPI
        auto owner = [&partition] (Long row) -> int
        {
            auto const it = std::upper_bound(
                partition.begin(), partition.end(), row);
            return static_cast<int>(it-partition.begin())-1;
        };

        Vector<int> request_send_counts(nprocs, 0);
        for (Long const row : needed) {
            if (row < local_begin || row >= local_end) {
                ++request_send_counts[owner(row)];
            }
        }
        Vector<int> request_send_displs(nprocs, 0);
        for (int p = 1; p < nprocs; ++p) {
            request_send_displs[p] =
                request_send_displs[p-1] + request_send_counts[p-1];
        }
        int const num_requests = request_send_displs.back()
            + request_send_counts.back();
        Gpu::PinnedVector<Long> requested_rows(num_requests);
        Vector<int> request_cursor = request_send_displs;
        for (Long const row : needed) {
            if (row < local_begin || row >= local_end) {
                requested_rows[request_cursor[owner(row)]++] = row;
            }
        }

        auto const comm = ParallelContext::CommunicatorSub();
        Vector<int> request_recv_counts(nprocs, 0);
        BL_MPI_REQUIRE(MPI_Alltoall(
            request_send_counts.data(), 1, MPI_INT,
            request_recv_counts.data(), 1, MPI_INT, comm));
        Vector<int> request_recv_displs(nprocs, 0);
        for (int p = 1; p < nprocs; ++p) {
            request_recv_displs[p] =
                request_recv_displs[p-1] + request_recv_counts[p-1];
        }
        int const num_incoming_requests = request_recv_displs.back()
            + request_recv_counts.back();
        Gpu::PinnedVector<Long> incoming_rows(num_incoming_requests);
        auto const mpi_long = ParallelDescriptor::Mpi_typemap<Long>::type();
        BL_MPI_REQUIRE(MPI_Alltoallv(
            requested_rows.data(), request_send_counts.data(),
            request_send_displs.data(), mpi_long,
            incoming_rows.data(), request_recv_counts.data(),
            request_recv_displs.data(), mpi_long, comm));

        Gpu::PinnedVector<Long> outgoing_lengths(num_incoming_requests);
        for (int i = 0; i < num_incoming_requests; ++i) {
            Long const row = incoming_rows[i];
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                row >= local_begin && row < local_end,
                "SpGEMM: received a request for a nonlocal right-operand row");
            Long const ilocal = row-local_begin;
            outgoing_lengths[i] =
                local.row_offset[ilocal+1] - local.row_offset[ilocal];
        }
        Gpu::PinnedVector<Long> incoming_lengths(num_requests);
        BL_MPI_REQUIRE(MPI_Alltoallv(
            outgoing_lengths.data(), request_recv_counts.data(),
            request_recv_displs.data(), mpi_long,
            incoming_lengths.data(), request_send_counts.data(),
            request_send_displs.data(), mpi_long, comm));

        Vector<int> value_send_counts(nprocs, 0);
        Vector<int> value_recv_counts(nprocs, 0);
        for (int p = 0; p < nprocs; ++p) {
            Long nsend = 0;
            for (int i = request_recv_displs[p];
                 i < request_recv_displs[p]+request_recv_counts[p]; ++i)
            {
                nsend += outgoing_lengths[i];
            }
            Long nrecv = 0;
            for (int i = request_send_displs[p];
                 i < request_send_displs[p]+request_send_counts[p]; ++i)
            {
                nrecv += incoming_lengths[i];
            }
            AMREX_ALWAYS_ASSERT(nsend <= std::numeric_limits<int>::max());
            AMREX_ALWAYS_ASSERT(nrecv <= std::numeric_limits<int>::max());
            value_send_counts[p] = static_cast<int>(nsend);
            value_recv_counts[p] = static_cast<int>(nrecv);
        }
        Vector<int> value_send_displs(nprocs, 0);
        Vector<int> value_recv_displs(nprocs, 0);
        for (int p = 1; p < nprocs; ++p) {
            value_send_displs[p] =
                value_send_displs[p-1] + value_send_counts[p-1];
            value_recv_displs[p] =
                value_recv_displs[p-1] + value_recv_counts[p-1];
        }
        int const num_values_to_send = value_send_displs.back()
            + value_send_counts.back();
        int const num_values_to_receive = value_recv_displs.back()
            + value_recv_counts.back();
        Gpu::PinnedVector<Long> outgoing_columns(num_values_to_send);
        Gpu::PinnedVector<T> outgoing_values(num_values_to_send);
        Vector<int> value_cursor = value_send_displs;
        for (int p = 0; p < nprocs; ++p) {
            for (int i = request_recv_displs[p];
                 i < request_recv_displs[p]+request_recv_counts[p]; ++i)
            {
                Long const ilocal = incoming_rows[i]-local_begin;
                for (Long q = local.row_offset[ilocal];
                     q < local.row_offset[ilocal+1]; ++q)
                {
                    int const out = value_cursor[p]++;
                    outgoing_columns[out] = local.col_index[q];
                    outgoing_values[out] = local.mat[q];
                }
            }
        }

        received_columns.resize(num_values_to_receive);
        received_values.resize(num_values_to_receive);
        auto const mpi_t = ParallelDescriptor::Mpi_typemap<T>::type();
        BL_MPI_REQUIRE(MPI_Alltoallv(
            outgoing_columns.data(), value_send_counts.data(),
            value_send_displs.data(), mpi_long,
            received_columns.data(), value_recv_counts.data(),
            value_recv_displs.data(), mpi_long, comm));
        BL_MPI_REQUIRE(MPI_Alltoallv(
            outgoing_values.data(), value_send_counts.data(),
            value_send_displs.data(), mpi_t,
            received_values.data(), value_recv_counts.data(),
            value_recv_displs.data(), mpi_t, comm));

        for (int p = 0; p < nprocs; ++p) {
            Long offset = value_recv_displs[p];
            for (int i = request_send_displs[p];
                 i < request_send_displs[p]+request_send_counts[p]; ++i)
            {
                Long const length = incoming_lengths[i];
                remote_rows.emplace(
                    requested_rows[i], RowSlice{offset, length});
                offset += length;
            }
            AMREX_ALWAYS_ASSERT(
                offset == value_recv_displs[p]+value_recv_counts[p]);
        }
#else
        amrex::ignore_unused(nprocs);
#endif

        host_csr_type compact;
        compact.row_offset.resize(needed.size()+1);
        compact.row_offset[0] = 0;
        for (std::size_t i = 0; i < needed.size(); ++i) {
            Long const row = needed[i];
            if (row >= local_begin && row < local_end) {
                Long const ilocal = row-local_begin;
                for (Long q = local.row_offset[ilocal];
                     q < local.row_offset[ilocal+1]; ++q)
                {
                    compact.col_index.push_back(local.col_index[q]);
                    compact.mat.push_back(local.mat[q]);
                }
            } else {
                auto const it = remote_rows.find(row);
                AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                    it != remote_rows.end(),
                    "SpGEMM: a requested right-operand row was not imported");
                auto const [offset,length] = it->second;
                for (Long q = offset; q < offset+length; ++q) {
                    compact.col_index.push_back(received_columns[q]);
                    compact.mat.push_back(received_values[q]);
                }
            }
            compact.row_offset[i+1] =
                static_cast<Long>(compact.mat.size());
        }
        compact.nnz = static_cast<Long>(compact.mat.size());

        for (Long& row : A.col_index) {
            auto const it = std::lower_bound(
                needed.begin(), needed.end(), row);
            AMREX_ALWAYS_ASSERT(it != needed.end() && *it == row);
            row = static_cast<Long>(it-needed.begin());
        }
        return compact;
    }

    /**
     * \brief Form a local CSR product with portable device kernels.
     *
     * The method expands all scalar products, sorts each product row, and
     * reduces entries with identical columns.
     *
     * \param hA Locally owned rows of the left operand.
     * \param hB Complete global rows of the right operand.
     * \param ncols Global number of product columns.
     * \return Device-accessible local CSR rows of the product.
     */
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
        // Long GPU rows use an out-of-place sort and replace these buffers.
        product_val = products.mat.data();
        product_col = products.col_index.data();

        container_type<Long> unique_counts(nrows);
        auto* AMREX_RESTRICT ucounts = unique_counts.data();
        ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
        {
            Long count = 0;
            Long p = product_row[i];
            while (p < product_row[i+1]) {
                Long const col = product_col[p];
                T value = T(0);
                do {
                    value += product_val[p];
                    ++p;
                } while (p < product_row[i+1]
                         && product_col[p] == col);
                if (value != T(0)) {
                    ++count;
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
                if (value != T(0)) {
                    AMREX_ASSERT(col >= 0 && col < ncols);
                    ccol[out] = col;
                    cval[out] = value;
                    ++out;
                }
            }
        });
        Gpu::streamSynchronize();
        return C;
    }

private:
    /**
     * \brief Copy arbitrary CSR storage into pinned host CSR storage.
     *
     * \tparam V Source CSR vector template.
     * \param dst Destination in pinned host memory.
     * \param src Source CSR.
     */
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

    /**
     * \brief Copy pinned-host CSR storage into matrix-allocator storage.
     *
     * \param dst Destination CSR.
     * \param src Source CSR in pinned host memory.
     */
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
 * \return Distributed sparse matrix containing \f$AB\f$.
 *
 * \tparam T Floating-point matrix scalar type.
 * \tparam Allocator Storage allocator shared by the operands and product.
 */
template <typename T, template <typename> class Allocator>
SpMatrix<T,Allocator>
SpGEMM (SpMatrix<T,Allocator> const& A,
        SpMatrix<T,Allocator> const& B,
        AlgPartition const& col_partition)
{
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        A.columnPartition().empty()
        || A.columnPartition() == B.partition(),
        "SpGEMM: left column partition does not match right row partition");
    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        B.columnPartition().empty()
        || B.columnPartition() == col_partition,
        "SpGEMM: right column partition does not match product column partition");
    auto hA = SpGEMMHelper<T,Allocator>::copy_local_global_csr(A);
    auto hB = SpGEMMHelper<T,Allocator>::import_required_rows(hA, B);
    for (auto const col : hB.col_index) {
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            col >= 0 && col < col_partition.numGlobalRows(),
            "SpGEMM: right column index is outside product column range");
    }
    auto C = SpGEMMHelper<T,Allocator>::multiply(
        hA, hB, col_partition.numGlobalRows());
    return SpMatrix<T,Allocator>(
        A.partition(), col_partition, std::move(C));
}

}

#endif

#ifndef AMREX_ALG_HALO_EXCHANGE_H_
#define AMREX_ALG_HALO_EXCHANGE_H_
#include <AMReX_Config.H>

#include <AMReX_AlgPartition.H>
#include <AMReX_Gpu.H>
#include <AMReX_ParallelContext.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_Utility.H>
#include <AMReX_Vector.H>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace amrex {

/**
 * \file AMReX_AlgHaloExchange.H
 *
 * Payload-typed sparse halo exchange for distributed algebra fields.
 */

/**
 * \brief Reusable sparse exchange plan for fields distributed by an
 * AlgPartition.
 *
 * The plan is defined from the global IDs needed by one rank. It exchanges
 * those requests once, stores device packing indices, and can subsequently
 * exchange fields of any type supported by
 * `ParallelDescriptor::Mpi_typemap`.
 */
class AlgHaloExchange
{
public:
    AlgHaloExchange () = default;

    /**
     * \brief Construct a sparse exchange plan.
     *
     * \param partition Distribution of the source field.
     * \param requested_global_ids Global IDs read by this rank. Local IDs and
     *        duplicates are permitted and are removed while defining the
     *        plan.
     */
    AlgHaloExchange (AlgPartition partition,
                     Vector<Long> requested_global_ids)
    {
        define(std::move(partition), std::move(requested_global_ids));
    }

    /**
     * \brief Define or replace the sparse exchange plan.
     *
     * \param partition Distribution of the source field.
     * \param requested_global_ids Global IDs read by this rank.
     */
    void define (AlgPartition partition,
                 Vector<Long> requested_global_ids)
    {
        m_partition = std::move(partition);
        int const myproc = ParallelDescriptor::MyProc();
        int const nprocs = ParallelDescriptor::NProcs();
        auto const& rows = m_partition.dataVector();
        AMREX_ALWAYS_ASSERT(
            static_cast<int>(rows.size()) == nprocs+1);
        m_local_begin = rows[myproc];
        m_local_end = rows[myproc+1];
        m_num_local = m_local_end-m_local_begin;

        std::sort(requested_global_ids.begin(),
                  requested_global_ids.end());
        requested_global_ids.erase(
            std::unique(requested_global_ids.begin(),
                        requested_global_ids.end()),
            requested_global_ids.end());
        for (Long const gid : requested_global_ids) {
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                gid >= 0 && gid < m_partition.numGlobalRows(),
                "AlgHaloExchange: requested global ID is out of range");
        }

        auto owner = [&rows] (Long gid) -> int
        {
            auto const it =
                std::upper_bound(rows.begin(), rows.end(), gid);
            return static_cast<int>(it-rows.begin())-1;
        };

        m_recv_counts.assign(nprocs, 0);
        for (Long const gid : requested_global_ids) {
            if (gid < m_local_begin || gid >= m_local_end) {
                ++m_recv_counts[owner(gid)];
            }
        }
        m_recv_displs.assign(nprocs, 0);
        for (int p = 1; p < nprocs; ++p) {
            m_recv_displs[p] =
                m_recv_displs[p-1] + m_recv_counts[p-1];
        }
        int const num_ghosts = m_recv_displs.back()
            + m_recv_counts.back();
        m_ghost_ids.resize(num_ghosts);
        Vector<int> cursor = m_recv_displs;
        for (Long const gid : requested_global_ids) {
            if (gid < m_local_begin || gid >= m_local_end) {
                m_ghost_ids[cursor[owner(gid)]++] = gid;
            }
        }

        m_send_counts.assign(nprocs, 0);
        Vector<Long> requested_from_this_rank;
#ifdef AMREX_USE_MPI
        auto const comm = ParallelContext::CommunicatorSub();
        BL_MPI_REQUIRE(MPI_Alltoall(
            m_recv_counts.data(), 1, MPI_INT,
            m_send_counts.data(), 1, MPI_INT, comm));
#else
        amrex::ignore_unused(nprocs);
#endif
        m_send_displs.assign(m_send_counts.size(), 0);
        for (int p = 1; p < static_cast<int>(m_send_counts.size()); ++p) {
            m_send_displs[p] =
                m_send_displs[p-1] + m_send_counts[p-1];
        }
        int const num_to_send = m_send_displs.back()
            + m_send_counts.back();
        requested_from_this_rank.resize(num_to_send);
#ifdef AMREX_USE_MPI
        auto const mpi_long =
            ParallelDescriptor::Mpi_typemap<Long>::type();
        BL_MPI_REQUIRE(MPI_Alltoallv(
            m_ghost_ids.data(), m_recv_counts.data(),
            m_recv_displs.data(), mpi_long,
            requested_from_this_rank.data(), m_send_counts.data(),
            m_send_displs.data(), mpi_long,
            ParallelContext::CommunicatorSub()));
#endif

        Gpu::PinnedVector<Long> host_send_indices(num_to_send);
        for (int i = 0; i < num_to_send; ++i) {
            Long const gid = requested_from_this_rank[i];
            AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
                gid >= m_local_begin && gid < m_local_end,
                "AlgHaloExchange: received a request for a nonlocal ID");
            host_send_indices[i] = gid-m_local_begin;
        }
        m_send_local_indices.resize(num_to_send);
        if (num_to_send > 0) {
#ifdef AMREX_USE_GPU
            Gpu::copyAsync(
                Gpu::hostToDevice, host_send_indices.begin(),
                host_send_indices.end(), m_send_local_indices.begin());
            Gpu::streamSynchronize();
#else
            std::copy(host_send_indices.begin(),
                      host_send_indices.end(),
                      m_send_local_indices.begin());
#endif
        }
    }

    //! Number of locally owned field entries.
    [[nodiscard]] Long numLocalEntries () const noexcept
    {
        return m_num_local;
    }

    //! Number of off-rank entries imported by the plan.
    [[nodiscard]] Long numGhostEntries () const noexcept
    {
        return static_cast<Long>(m_ghost_ids.size());
    }

    /**
     * \brief Return the packed local-or-ghost slot for a global ID.
     *
     * Local entries occupy `[0,numLocalEntries())`; imported entries follow
     * in the order returned by `ghostGlobalIds()`.
     */
    [[nodiscard]] Long localOrGhostIndex (Long gid) const
    {
        if (gid >= m_local_begin && gid < m_local_end) {
            return gid-m_local_begin;
        }
        auto const it =
            std::lower_bound(m_ghost_ids.begin(), m_ghost_ids.end(), gid);
        AMREX_ALWAYS_ASSERT(
            it != m_ghost_ids.end() && *it == gid);
        return m_num_local + static_cast<Long>(it-m_ghost_ids.begin());
    }

    //! Global IDs imported by this rank, in packed ghost order.
    [[nodiscard]] Vector<Long> const& ghostGlobalIds () const noexcept
    {
        return m_ghost_ids;
    }

    //! Estimated persistent storage used by this exchange plan on this rank.
    [[nodiscard]] std::size_t estimatedBytes () const noexcept
    {
        return m_ghost_ids.size()*sizeof(Long)
            + (m_send_counts.size()+m_send_displs.size()
               +m_recv_counts.size()+m_recv_displs.size())*sizeof(int)
            + m_send_local_indices.size()*sizeof(Long);
    }

    /**
     * \brief Exchange a distributed field using this plan.
     *
     * \tparam U Payload type with an AMReX MPI type mapping.
     * \param local Device-accessible local field of length
     *        `numLocalEntries()`.
     * \return Device vector containing local entries followed by ghosts.
     */
    template <typename U>
    [[nodiscard]] Gpu::DeviceVector<U> exchange (U const* local) const
    {
        Long const nghost = numGhostEntries();
        Gpu::DeviceVector<U> result(m_num_local+nghost);
        if (m_num_local > 0) {
#ifdef AMREX_USE_GPU
            Gpu::copyAsync(Gpu::deviceToDevice, local,
                           local+m_num_local, result.begin());
#else
            std::copy(local, local+m_num_local, result.begin());
#endif
        }

#ifdef AMREX_USE_MPI
        int const send_count = m_send_displs.back()
            + m_send_counts.back();
        Gpu::DeviceVector<U> device_send(send_count);
        auto const* AMREX_RESTRICT indices =
            m_send_local_indices.data();
        auto* AMREX_RESTRICT packed = device_send.data();
        ParallelFor(send_count,
                    [=] AMREX_GPU_DEVICE (int i) noexcept
        {
            packed[i] = local[indices[i]];
        });

        Gpu::PinnedVector<U> host_send(send_count);
        Gpu::PinnedVector<U> host_recv(nghost);
#ifdef AMREX_USE_GPU
        if (send_count > 0) {
            Gpu::copyAsync(Gpu::deviceToHost, device_send.begin(),
                           device_send.end(), host_send.begin());
        }
        Gpu::streamSynchronize();
#else
        std::copy(device_send.begin(), device_send.end(),
                  host_send.begin());
#endif
        auto const mpi_u =
            ParallelDescriptor::Mpi_typemap<U>::type();
        BL_MPI_REQUIRE(MPI_Alltoallv(
            host_send.data(), m_send_counts.data(),
            m_send_displs.data(), mpi_u,
            host_recv.data(), m_recv_counts.data(),
            m_recv_displs.data(), mpi_u,
            ParallelContext::CommunicatorSub()));
        if (nghost > 0) {
#ifdef AMREX_USE_GPU
            Gpu::copyAsync(Gpu::hostToDevice, host_recv.begin(),
                           host_recv.end(), result.begin()+m_num_local);
            Gpu::streamSynchronize();
#else
            std::copy(host_recv.begin(), host_recv.end(),
                      result.begin()+m_num_local);
#endif
        }
#else
        amrex::ignore_unused(local);
#endif
        return result;
    }

private:
    AlgPartition m_partition;
    Long m_local_begin = 0;
    Long m_local_end = 0;
    Long m_num_local = 0;
    Vector<Long> m_ghost_ids;
    Vector<int> m_send_counts;
    Vector<int> m_send_displs;
    Vector<int> m_recv_counts;
    Vector<int> m_recv_displs;
    Gpu::DeviceVector<Long> m_send_local_indices;
};

}

#endif

#ifndef AMREX_L1_JACOBI_SMOOTHER_H_
#define AMREX_L1_JACOBI_SMOOTHER_H_
#include <AMReX_Config.H>

#include <AMReX_AlgVector.H>
#include <AMReX_Math.H>
#include <AMReX_ParallelReduce.H>
#include <AMReX_Reduce.H>
#include <AMReX_SpMV.H>

namespace amrex {

/**
 * \file AMReX_L1JacobiSmoother.H
 *
 * L1-Jacobi smoothing for distributed sparse matrices.
 */

/**
 * \brief L1-Jacobi smoother with cached absolute row sums.
 *
 * One nonzero-guess sweep applies
 * \f[
 * x_i \leftarrow x_i+
 * \frac{b_i-(Ax)_i}{\sum_j |a_{ij}|}.
 * \f]
 * When the input correction is known to be zero, the matrix-vector product
 * is omitted and the result is `x_i=b_i/d_i`.
 *
 * \tparam T Floating-point matrix and vector scalar type.
 */
template <typename T>
class L1JacobiSmoother
{
public:
    L1JacobiSmoother () = default;

    //! Construct and cache row denominators for \p A.
    explicit L1JacobiSmoother (SpMatrix<T> const& A)
    {
        define(A);
    }

    /**
     * \brief Bind to \p A and cache its absolute row sums.
     *
     * The matrix must outlive this smoother and must not be modified after
     * this call.
     */
    void define (SpMatrix<T> const& A)
    {
        m_matrix = &A;
        m_denominator.define(A.partition());
        auto const a = A.const_parcsr();
        auto* AMREX_RESTRICT denominator = m_denominator.data();
        Long const nrows = A.numLocalRows();
        int failures = Reduce::Sum<int>(
            nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept -> int
            {
                T row_l1 = T(0);
                for (Long p = a.csr0.row_offset[i];
                     p < a.csr0.row_offset[i+1]; ++p)
                {
                    row_l1 += Math::abs(a.csr0.mat[p]);
                }
                if (a.csr1.nnz > 0 && a.row_map != nullptr) {
                    Long const remote_row = a.row_map[i];
                    if (remote_row >= 0) {
                        for (Long p = a.csr1.row_offset[remote_row];
                             p < a.csr1.row_offset[remote_row+1]; ++p)
                        {
                            row_l1 += Math::abs(a.csr1.mat[p]);
                        }
                    }
                }
                denominator[i] = row_l1;
                return row_l1 <= T(0);
            });
        ParallelAllReduce::Sum(
            failures, ParallelContext::CommunicatorSub());
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            failures == 0,
            "L1JacobiSmoother requires a nonzero absolute row sum");
    }

    /**
     * \brief Apply one L1-Jacobi sweep.
     *
     * \param x Correction updated in place.
     * \param b Right-hand side.
     * \param work Matrix-vector workspace with the matrix row partition.
     * \param zero_initial_guess Whether \p x is known to be zero on entry.
     */
    void sweep (AlgVector<T>& x, AlgVector<T> const& b,
                AlgVector<T>& work, bool zero_initial_guess) const
    {
        AMREX_ALWAYS_ASSERT(m_matrix != nullptr);
        AMREX_ALWAYS_ASSERT(x.partition() == m_matrix->partition());
        AMREX_ALWAYS_ASSERT(b.partition() == m_matrix->partition());
        AMREX_ALWAYS_ASSERT(work.partition() == m_matrix->partition());
        auto const* AMREX_RESTRICT pb = b.data();
        auto const* AMREX_RESTRICT pd = m_denominator.data();
        auto* AMREX_RESTRICT px = x.data();
        Long const nrows = x.numLocalRows();
        if (zero_initial_guess) {
            ParallelFor(nrows,
                        [=] AMREX_GPU_DEVICE (Long i) noexcept
            {
                px[i] = pb[i]/pd[i];
            });
        } else {
            SpMV(work, *m_matrix, x);
            auto const* AMREX_RESTRICT pax = work.data();
            ParallelFor(nrows,
                        [=] AMREX_GPU_DEVICE (Long i) noexcept
            {
                px[i] += (pb[i]-pax[i])/pd[i];
            });
        }
    }

    //! Return the cached absolute row sums.
    [[nodiscard]] AlgVector<T> const& denominator () const noexcept
    {
        return m_denominator;
    }

private:
    SpMatrix<T> const* m_matrix = nullptr;
    AlgVector<T> m_denominator;
};

}

#endif

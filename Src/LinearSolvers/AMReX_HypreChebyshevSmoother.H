#ifndef AMREX_HYPRE_CHEBYSHEV_SMOOTHER_H_
#define AMREX_HYPRE_CHEBYSHEV_SMOOTHER_H_
/*
 * Chebyshev algorithm derived from HYPRE, Copyright (c) 1998 Lawrence
 * Livermore National Security, LLC and other HYPRE Project Developers.
 * HYPRE is available under the Apache-2.0 OR MIT license.
 */
#include <AMReX_Config.H>

#include <AMReX_AlgVecUtil.H>
#include <AMReX_BLProfiler.H>
#include <AMReX_Gpu.H>
#include <AMReX_Math.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_ParallelReduce.H>
#include <AMReX_Reduce.H>
#include <AMReX_SpMatrix.H>
#include <AMReX_SpMV.H>

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace amrex {

/**
 * \brief Diagonally scaled Chebyshev relaxation matching HYPRE BoomerAMG.
 *
 * This follows HYPRE relaxation type 16, including its default order-two
 * standard polynomial, diagonal scaling, either a CG/Lanczos or Gershgorin
 * eigenvalue estimate, spectral fraction 0.3, coefficient formulas, and
 * Horner apply. The implementation follows par_cheby_setup.c,
 * par_cheby_solve.c, and the eigenvalue estimators in par_relax_more.c from
 * HYPRE.
 */
template <typename T>
class HypreChebyshevSmoother
{
public:
    static_assert(std::is_floating_point_v<T>);

    struct Options {
        int order = 2;
        //! Zero selects Gershgorin; a positive value selects CG/Lanczos.
        int eigenvalue_iterations = 10;
        T fraction = T(0.3);
        int variant = 0;
        bool scale = true;
    };

    explicit HypreChebyshevSmoother (SpMatrix<T> const& A,
                                     Options options = {})
        : m_matrix(&A), m_options(options), m_scaling(A.partition()),
          m_v(A.partition()), m_r(A.partition()),
          m_original(A.partition()), m_tmp(A.partition())
    {
        m_options.order = std::clamp(m_options.order, 1, 4);
        AMREX_ALWAYS_ASSERT(m_options.eigenvalue_iterations >= 0);
        AMREX_ALWAYS_ASSERT(m_options.variant == 0 || m_options.variant == 1);
        define_scaling();
        if (m_options.eigenvalue_iterations == 0) {
            estimate_eigenvalues_gershgorin();
        } else {
            estimate_eigenvalues_cg();
        }
        define_coefficients();
    }

    void sweep (AlgVector<T>& x, AlgVector<T> const& b)
    {
        AMREX_ALWAYS_ASSERT(x.partition() == m_matrix->partition());
        AMREX_ALWAYS_ASSERT(b.partition() == m_matrix->partition());
        SpMV(m_tmp, *m_matrix, x);
        auto const* AMREX_RESTRICT pb = b.data();
        auto const* AMREX_RESTRICT ptmp = m_tmp.data();
        auto const* AMREX_RESTRICT pscale = m_scaling.data();
        auto* AMREX_RESTRICT pr = m_r.data();
        auto* AMREX_RESTRICT po = m_original.data();
        auto* AMREX_RESTRICT px = x.data();
        Long const nrows = x.numLocalRows();
        T const leading = m_coefficients[m_options.order-1];
        bool const scale = m_options.scale;
        ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
        {
            T const ds = scale ? pscale[i] : T(1);
            pr[i] = ds*(pb[i]-ptmp[i]);
            po[i] = px[i];
            px[i] = leading*pr[i];
        });
        for (int icoef = m_options.order-2; icoef >= 0; --icoef) {
            auto const* AMREX_RESTRICT pq = x.data();
            auto* AMREX_RESTRICT psq = m_tmp.data();
            ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
            {
                psq[i] = (scale ? pscale[i] : T(1))*pq[i];
            });
            SpMV(m_v, *m_matrix, m_tmp);
            auto const* AMREX_RESTRICT pv = m_v.data();
            T const coefficient = m_coefficients[icoef];
            ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
            {
                px[i] = coefficient*pr[i]
                    +(scale ? pscale[i] : T(1))*pv[i];
            });
        }
        ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
        {
            px[i] = po[i]+(scale ? pscale[i] : T(1))*px[i];
        });
        Gpu::streamSynchronize();
    }

    [[nodiscard]] T minEigenvalue () const noexcept { return m_min_eigenvalue; }
    [[nodiscard]] T maxEigenvalue () const noexcept { return m_max_eigenvalue; }
    [[nodiscard]] Vector<T> const& coefficients () const noexcept
    {
        return m_coefficients;
    }
    [[nodiscard]] AlgVector<T> const& scaling () const noexcept
    {
        return m_scaling;
    }

private:
    void define_scaling ()
    {
        BL_PROFILE("Chebyshev::define_scaling()");
        if (!m_options.scale) {
            m_scaling.setVal(T(1));
            return;
        }
        auto const& diagonal = m_matrix->diagonalVector();
        auto const* AMREX_RESTRICT pd = diagonal.data();
        auto* AMREX_RESTRICT ps = m_scaling.data();
        Long const nrows = m_scaling.numLocalRows();
        int failures = Reduce::Sum<int>(
            nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept -> int
            {
                T const magnitude = Math::abs(pd[i]);
                ps[i] = T(1)/std::sqrt(magnitude);
                return magnitude == T(0);
            });
        ParallelAllReduce::Sum(failures, ParallelContext::CommunicatorSub());
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            failures == 0, "Chebyshev scaling requires a nonzero diagonal");
    }

    static void set_hypre_random_values (AlgVector<T>& values)
    {
        constexpr int multiplier = 16807;
        constexpr int modulus = 2147483647;
        constexpr int quotient = 127773;
        constexpr int remainder = 2836;
        int seed = ParallelDescriptor::MyProc()+1;
        Gpu::PinnedVector<T> host(values.numLocalRows());
        for (Long i = 0; i < values.numLocalRows(); ++i) {
            int const high = seed/quotient;
            int const low = seed%quotient;
            int const trial = multiplier*low-remainder*high;
            seed = trial > 0 ? trial : trial+modulus;
            host[i] = T(2)*(T(seed)/T(modulus))-T(1);
        }
        if (!host.empty()) {
            Gpu::copyAsync(Gpu::hostToDevice, host.begin(), host.end(),
                           values.data());
            Gpu::streamSynchronize();
        }
    }

    static T stable_hypot (T a, T b)
    {
        T p = std::max(std::abs(a), std::abs(b));
        if (p == T(0)) { return p; }
        T ratio = std::min(std::abs(a), std::abs(b))/p;
        T r = ratio*ratio;
        while (true) {
            T const t = r+T(4);
            if (t == T(4)) { return p; }
            T const s = r/t;
            T const u = T(2)*s+T(1);
            p *= u;
            ratio = s/u;
            r *= ratio*ratio;
        }
    }

    // EISPACK TQL1, as used by HYPRE's CG eigenvalue estimator.
    static int tridiagonal_eigenvalues (Vector<T>& diagonal,
                                        Vector<T>& offdiagonal)
    {
        int const n = static_cast<int>(diagonal.size());
        if (n <= 1) { return 0; }
        Vector<T> d(n+1, T(0));
        Vector<T> e(n+1, T(0));
        for (int i = 0; i < n; ++i) {
            d[i+1] = diagonal[i];
            e[i+1] = offdiagonal[i];
        }
        for (int i = 2; i <= n; ++i) { e[i-1] = e[i]; }
        T f = T(0);
        T tst1 = T(0);
        e[n] = T(0);
        for (int l = 1; l <= n; ++l) {
            int iteration = 0;
            tst1 = std::max(tst1, std::abs(d[l])+std::abs(e[l]));
            int m = l;
            for (; m <= n; ++m) {
                if (tst1+std::abs(e[m]) == tst1) { break; }
            }
            while (m != l) {
                if (iteration == 30) { return l; }
                ++iteration;
                int const l1 = l+1;
                int const l2 = l1+1;
                T const g = d[l];
                T const q = (d[l1]-g)/(T(2)*e[l]);
                T const radius = stable_hypot(q, T(1));
                T const sign = q < T(0) ? T(-1) : T(1);
                d[l] = e[l]/(q+sign*radius);
                d[l1] = e[l]*(q+sign*radius);
                T const dl1 = d[l1];
                T const shift = g-d[l];
                for (int i = l2; i <= n; ++i) { d[i] -= shift; }
                f += shift;
                T p = d[m];
                T c = T(1);
                T c2 = c;
                T c3 = T(0);
                T s = T(0);
                T s2 = T(0);
                T const el1 = e[l1];
                for (int ii = 1; ii <= m-l; ++ii) {
                    c3 = c2;
                    c2 = c;
                    s2 = s;
                    int const i = m-ii;
                    T const eg = c*e[i];
                    T const h = c*p;
                    T const radius2 = stable_hypot(p, e[i]);
                    e[i+1] = s*radius2;
                    s = e[i]/radius2;
                    c = p/radius2;
                    p = c*d[i]-s*eg;
                    d[i+1] = h+s*(c*eg+s*d[i]);
                }
                p = -s*s2*c3*el1*e[l]/dl1;
                e[l] = s*p;
                d[l] = c*p;
                if (tst1+std::abs(e[l]) == tst1) { break; }
            }
            T const p = d[l]+f;
            int i = l;
            while (i > 1 && p < d[i-1]) {
                d[i] = d[i-1];
                --i;
            }
            d[i] = p;
        }
        for (int i = 0; i < n; ++i) { diagonal[i] = d[i+1]; }
        return 0;
    }

    /**
     * \brief Estimate the spectrum with HYPRE's Gershgorin algorithm.
     *
     * The bounds depend only on complete global matrix rows and a global
     * minimum/maximum, so the mathematical estimate is independent of how
     * rows are partitioned among MPI ranks.
     */
    void estimate_eigenvalues_gershgorin ()
    {
        BL_PROFILE("Chebyshev::estimate_eigenvalues_gershgorin()");
        auto const a = m_matrix->const_parcsr();
        auto const& diagonal = m_matrix->diagonalVector();
        auto const* AMREX_RESTRICT pd = diagonal.data();
        Long const nrows = m_matrix->numLocalRows();
        bool const scale = m_options.scale;

        ReduceOps<ReduceOpMin,ReduceOpMax> reduce_op;
        ReduceData<T,T> reduce_data(reduce_op);
        using ReduceTuple = typename decltype(reduce_data)::Type;
        reduce_op.eval(
            nrows, reduce_data,
            [=] AMREX_GPU_DEVICE (Long i) noexcept -> ReduceTuple
            {
                T radius = T(0);
                for (Long p = a.csr0.row_offset[i];
                     p < a.csr0.row_offset[i+1]; ++p)
                {
                    radius += Math::abs(a.csr0.mat[p]);
                }
                if (a.csr1.nnz > 0 && a.row_map != nullptr) {
                    Long const remote_row = a.row_map[i];
                    if (remote_row >= 0) {
                        for (Long p = a.csr1.row_offset[remote_row];
                             p < a.csr1.row_offset[remote_row+1]; ++p)
                        {
                            radius += Math::abs(a.csr1.mat[p]);
                        }
                    }
                }
                T const diagonal_value = pd[i];
                radius -= Math::abs(diagonal_value);
                T lower = diagonal_value-radius;
                T upper = diagonal_value+radius;
                if (scale) {
                    T const diagonal_magnitude = Math::abs(diagonal_value);
                    lower /= diagonal_magnitude;
                    upper /= diagonal_magnitude;
                }
                return {lower,upper};
            });

        auto bounds = reduce_data.value(reduce_op);
        T lower = amrex::get<0>(bounds);
        T upper = amrex::get<1>(bounds);
        ParallelAllReduce::Min(
            lower, ParallelContext::CommunicatorSub());
        ParallelAllReduce::Max(
            upper, ParallelContext::CommunicatorSub());

        if (Math::abs(lower) > Math::abs(upper)) {
            m_min_eigenvalue = lower;
            m_max_eigenvalue = std::min(T(0), upper);
        } else {
            m_min_eigenvalue = std::max(lower, T(0));
            m_max_eigenvalue = upper;
        }
    }

    /** Estimate the spectrum with HYPRE's CG/Lanczos algorithm. */
    void estimate_eigenvalues_cg ()
    {
        BL_PROFILE("Chebyshev::estimate_eigenvalues_cg()");
        int const iterations = m_options.eigenvalue_iterations;
        AlgVector<T> p(m_matrix->partition());
        AlgVector<T> s(m_matrix->partition());
        AlgVector<T> r(m_matrix->partition());
        AlgVector<T> u(m_matrix->partition());
        set_hypre_random_values(r);
        Vector<T> diagonal(iterations+1, T(0));
        Vector<T> offdiagonal(iterations+1, T(0));
        T gamma = T(0);
        int completed = 0;
        for (; completed < iterations; ++completed) {
            s.copy(r);
            T const gamma_old = gamma;
            gamma = Dot(r, s);
            if (gamma < std::numeric_limits<T>::epsilon()) { break; }
            T beta = T(1);
            if (completed == 0) {
                p.copy(s);
            } else {
                beta = gamma/gamma_old;
                Xpay(p, beta, s);
            }
            if (m_options.scale) {
                auto const* AMREX_RESTRICT pp = p.data();
                auto const* AMREX_RESTRICT pscale = m_scaling.data();
                auto* AMREX_RESTRICT pu = u.data();
                Long const nrows = p.numLocalRows();
                ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
                {
                    pu[i] = pscale[i]*pp[i];
                });
                SpMV(s, *m_matrix, u);
                auto* AMREX_RESTRICT ps = s.data();
                ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) noexcept
                {
                    ps[i] *= pscale[i];
                });
            } else {
                SpMV(s, *m_matrix, p);
            }
            T const alpha_inverse = Dot(s, p)/gamma;
            T const alpha = T(1)/alpha_inverse;
            diagonal[completed+1] = alpha_inverse;
            diagonal[completed] = diagonal[completed]*beta+alpha_inverse;
            offdiagonal[completed+1] = alpha_inverse;
            offdiagonal[completed] *= std::sqrt(beta);
            Axpy(r, -alpha, s);
        }
        diagonal.resize(completed);
        offdiagonal.resize(completed);
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            completed > 0 && tridiagonal_eigenvalues(diagonal, offdiagonal) == 0,
            "Chebyshev eigenvalue estimation failed");
        m_min_eigenvalue = diagonal.front();
        m_max_eigenvalue = diagonal.back();
    }

    void define_coefficients ()
    {
        int const degree = m_options.order-1;
        m_coefficients.assign(m_options.order, T(0));
        T upper;
        T lower;
        if (m_max_eigenvalue <= T(0)) {
            upper = m_min_eigenvalue*T(1.1);
            lower = m_max_eigenvalue
                -(m_max_eigenvalue-upper)*m_options.fraction;
        } else {
            upper = m_max_eigenvalue*T(1.1);
            lower = (upper-m_min_eigenvalue)*m_options.fraction
                +m_min_eigenvalue;
        }
        T const theta = (upper+lower)/T(2);
        T const delta = (upper-lower)/T(2);
        T den = T(0);
        if (m_options.variant == 1) {
            switch (degree) {
            case 0:
                m_coefficients[0] = theta != T(0) ? T(1)/theta : T(0);
                break;
            case 1:
                den = theta*theta+delta*theta;
                if (den != T(0)) {
                    m_coefficients[0] = (delta+T(2)*theta)/den;
                    m_coefficients[1] = -T(1)/den;
                }
                break;
            case 2:
                den = T(2)*delta*theta*theta-delta*delta*theta
                    -delta*delta*delta+T(2)*theta*theta*theta;
                if (den != T(0)) {
                    m_coefficients[0] = (T(4)*delta*theta-delta*delta
                        +T(6)*theta*theta)/den;
                    m_coefficients[1] = -(T(2)*delta+T(6)*theta)/den;
                    m_coefficients[2] = T(2)/den;
                }
                break;
            case 3:
                den = -T(4)*delta*theta*theta*theta
                    +T(3)*delta*delta*theta*theta
                    +T(3)*delta*delta*delta*theta
                    -T(4)*theta*theta*theta*theta;
                if (den != T(0)) {
                    m_coefficients[0] = (T(6)*delta*delta*theta
                        -T(12)*delta*theta*theta+T(3)*delta*delta*delta
                        -T(16)*theta*theta*theta)/den;
                    m_coefficients[1] = (T(12)*delta*theta-T(3)*delta*delta
                        +T(24)*theta*theta)/den;
                    m_coefficients[2] = -(T(4)*delta+T(16)*theta)/den;
                    m_coefficients[3] = T(4)/den;
                }
                break;
            }
        } else {
            switch (degree) {
            case 0:
                m_coefficients[0] = theta != T(0) ? T(1)/theta : T(0);
                break;
            case 1:
                den = delta*delta-T(2)*theta*theta;
                if (den != T(0)) {
                    m_coefficients[0] = -T(4)*theta/den;
                    m_coefficients[1] = T(2)/den;
                }
                break;
            case 2:
                den = T(3)*delta*delta*theta-T(4)*theta*theta*theta;
                if (den != T(0)) {
                    m_coefficients[0] = (T(3)*delta*delta
                        -T(12)*theta*theta)/den;
                    m_coefficients[1] = T(12)*theta/den;
                    m_coefficients[2] = -T(4)/den;
                }
                break;
            case 3:
                den = delta*delta*delta*delta
                    -T(8)*delta*delta*theta*theta
                    +T(8)*theta*theta*theta*theta;
                if (den != T(0)) {
                    m_coefficients[0] = (T(32)*theta*theta*theta
                        -T(16)*delta*delta*theta)/den;
                    m_coefficients[1] = (T(8)*delta*delta
                        -T(48)*theta*theta)/den;
                    m_coefficients[2] = T(32)*theta/den;
                    m_coefficients[3] = -T(8)/den;
                }
                break;
            }
        }
    }

    SpMatrix<T> const* m_matrix = nullptr;
    Options m_options;
    AlgVector<T> m_scaling;
    AlgVector<T> m_v;
    AlgVector<T> m_r;
    AlgVector<T> m_original;
    AlgVector<T> m_tmp;
    Vector<T> m_coefficients;
    T m_min_eigenvalue = T(0);
    T m_max_eigenvalue = T(0);
};

}

#endif

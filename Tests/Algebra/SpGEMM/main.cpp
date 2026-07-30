#include <AMReX.H>
#include <AMReX_AlgVecUtil.H>
#include <AMReX_SpGEMM.H>
#include <AMReX_SpMV.H>

using namespace amrex;

/**
 * \brief Verify rectangular distributed SpGEMM through matrix action.
 *
 * \param argc Command-line argument count.
 * \param argv Command-line argument vector.
 * \return Zero after the product-action assertion passes.
 */
int
main (int argc, char* argv[])
{
    amrex::Initialize(argc, argv);

    {
        constexpr Long m = 29;
        constexpr Long k = 23;
        constexpr Long n = 31;

        AlgPartition const m_partition(m);
        AlgPartition const k_partition(k);
        AlgPartition const n_partition(n);

        SpMatrix<Real> A(m_partition, 3);
        A.setVal(
            [=] AMREX_GPU_DEVICE (Long row, Long* col, Real* val) noexcept
            {
                col[0] = row % k;
                col[1] = (row + 1) % k;
                col[2] = (row + 7) % k;
                val[0] = Real(1.0) + Real(0.01)*Real(row);
                val[1] = Real(-0.25);
                val[2] = Real(0.125);
            },
            CsrSorted{false});

        SpMatrix<Real> B(k_partition, 3);
        B.setVal(
            [=] AMREX_GPU_DEVICE (Long row, Long* col, Real* val) noexcept
            {
                col[0] = row % n;
                col[1] = (row + 2) % n;
                col[2] = (row + 9) % n;
                val[0] = Real(0.5) + Real(0.02)*Real(row);
                val[1] = Real(-0.75);
                val[2] = Real(0.0625);
            },
            CsrSorted{false});

        AlgVector<Real> x(n_partition);
        auto* px = x.data();
        auto const xbegin = x.globalBegin();
        ParallelFor(x.numLocalRows(),
                    [=] AMREX_GPU_DEVICE (Long i) noexcept
        {
            auto const row = i + xbegin;
            px[i] = Real(1.0) + Real(0.125)*Real(row % 11);
        });

        AlgVector<Real> bx(k_partition);
        AlgVector<Real> expected(m_partition);
        SpMV(bx, B, x);
        SpMV(expected, A, bx);

        auto C = SpGEMM(A, B, n_partition);
        AlgVector<Real> actual(m_partition);
        SpMV(actual, C, x);
        Axpy(actual, Real(-1.0), expected);

        Real const error = actual.norminf();
        Real const tolerance =
            (sizeof(Real) == sizeof(float)) ? Real(2.e-5) : Real(2.e-13);
        amrex::Print() << "SpGEMM rectangular action error: " << error << '\n';
        AMREX_ALWAYS_ASSERT(error < tolerance);
    }

    amrex::Finalize();
}

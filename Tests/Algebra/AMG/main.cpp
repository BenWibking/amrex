#include <AMReX.H>
#include <AMReX_AMG.H>
#include <AMReX_GMRES_MV.H>
#include <AMReX_Math.H>
#include <AMReX_SpMV.H>

#ifdef AMREX_USE_HYPRE
#include <HYPRE.h>
#include <HYPRE_IJ_mv.h>
#include <HYPRE_parcsr_ls.h>
#endif

#include <cmath>
#include <limits>

using namespace amrex;

namespace {

void
fill_exact (AlgVector<Real>& exact)
{
    auto* x = exact.data();
    auto const begin = exact.globalBegin();
    auto const n = exact.numGlobalRows();
    ParallelFor(exact.numLocalRows(),
                [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        auto const gid = i + begin;
        Real const phase =
            Real(2.0)*Real(3.1415926535897932384626433832795)
            * Real(gid)/Real(n);
        x[i] = Real(1.0) + Real(0.5)*std::sin(phase)
            + Real(0.125)*std::cos(Real(3.0)*phase);
    });
}

Real
true_residual (SpMatrix<Real> const& A, AlgVector<Real> const& x,
               AlgVector<Real> const& b)
{
    AlgVector<Real> residual(b.partition());
    SpMV(residual, A, x);
    LinComb(residual, Real(1), b, Real(-1), residual);
    return residual.norm2();
}

#ifdef AMREX_USE_HYPRE
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
HYPRE_Real
hypre_exact_value (Long gid, Long n) noexcept
{
    HYPRE_Real const phase =
        HYPRE_Real(2.0)
        * HYPRE_Real(3.1415926535897932384626433832795)
        * HYPRE_Real(gid)/HYPRE_Real(n);
    return HYPRE_Real(1.0)
        + HYPRE_Real(0.5)*std::sin(phase)
        + HYPRE_Real(0.125)*std::cos(HYPRE_Real(3.0)*phase);
}

AlgVector<Real>
solve_with_boomeramg (AlgPartition const& partition, Real tolerance,
                      int maximum_iterations, int& iterations)
{
    auto const comm = ParallelContext::CommunicatorSub();
    auto const begin = partition[ParallelDescriptor::MyProc()];
    auto const end = partition[ParallelDescriptor::MyProc()+1];
    auto const nlocal = end-begin;
    auto const n = partition.numGlobalRows();
    AMREX_ALWAYS_ASSERT(nlocal <= std::numeric_limits<HYPRE_Int>::max());

    HYPRE_IJMatrix ij_A = nullptr;
    HYPRE_IJVector ij_b = nullptr;
    HYPRE_IJVector ij_x = nullptr;
    HYPRE_Solver solver = nullptr;

    auto const ilower = static_cast<HYPRE_BigInt>(begin);
    auto const iupper = static_cast<HYPRE_BigInt>(end-1);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJMatrixCreate(comm, ilower, iupper, ilower, iupper,
                             &ij_A) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJMatrixSetObjectType(ij_A, HYPRE_PARCSR) == 0);
    AMREX_ALWAYS_ASSERT(HYPRE_IJMatrixInitialize(ij_A) == 0);

    Gpu::DeviceVector<HYPRE_Int> row_nnz(nlocal);
    Gpu::DeviceVector<HYPRE_BigInt> rows(nlocal);
    Gpu::DeviceVector<HYPRE_BigInt> columns(3*nlocal);
    Gpu::DeviceVector<HYPRE_Complex> matrix_values(3*nlocal);
    Gpu::DeviceVector<HYPRE_Complex> rhs_values(nlocal);
    auto* AMREX_RESTRICT pncols = row_nnz.data();
    auto* AMREX_RESTRICT prows = rows.data();
    auto* AMREX_RESTRICT pcols = columns.data();
    auto* AMREX_RESTRICT pmat = matrix_values.data();
    auto* AMREX_RESTRICT prhs = rhs_values.data();
    ParallelFor(nlocal, [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        Long const row = i + begin;
        pncols[i] = 3;
        prows[i] = static_cast<HYPRE_BigInt>(row);
        pcols[3*i  ] = static_cast<HYPRE_BigInt>((row+n-1) % n);
        pcols[3*i+1] = static_cast<HYPRE_BigInt>(row);
        pcols[3*i+2] = static_cast<HYPRE_BigInt>((row+1) % n);
        pmat[3*i  ] = HYPRE_Complex(-1);
        pmat[3*i+1] = HYPRE_Complex(3);
        pmat[3*i+2] = HYPRE_Complex(-1);
        prhs[i] = HYPRE_Complex(
            HYPRE_Real(3.0)*hypre_exact_value(row, n)
            - hypre_exact_value((row+n-1) % n, n)
            - hypre_exact_value((row+1) % n, n));
    });
    Gpu::streamSynchronize();
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJMatrixSetValues(
            ij_A, static_cast<HYPRE_Int>(nlocal), row_nnz.data(),
            rows.data(), columns.data(), matrix_values.data()) == 0);
    AMREX_ALWAYS_ASSERT(HYPRE_IJMatrixAssemble(ij_A) == 0);

    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorCreate(comm, ilower, iupper, &ij_b) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorSetObjectType(ij_b, HYPRE_PARCSR) == 0);
    AMREX_ALWAYS_ASSERT(HYPRE_IJVectorInitialize(ij_b) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorSetValues(
            ij_b, static_cast<HYPRE_Int>(nlocal),
            rows.data(), rhs_values.data()) == 0);
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
        HYPRE_IJMatrixGetObject(ij_A, reinterpret_cast<void**>(&par_A)) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorGetObject(ij_b, reinterpret_cast<void**>(&par_b)) == 0);
    AMREX_ALWAYS_ASSERT(
        HYPRE_IJVectorGetObject(ij_x, reinterpret_cast<void**>(&par_x)) == 0);

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
    HYPRE_BoomerAMGSetTol(solver, static_cast<HYPRE_Real>(tolerance));
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
            rows.data(), solution_values.data()) == 0);
    Gpu::hypreSynchronize();

    AlgVector<Real> result(partition);
    auto const* AMREX_RESTRICT phypre = solution_values.data();
    auto* AMREX_RESTRICT presult = result.data();
    ParallelFor(nlocal, [=] AMREX_GPU_DEVICE (Long i) noexcept
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

}

int
main (int argc, char* argv[])
{
    amrex::Initialize(argc, argv);

    constexpr Long n = 128;
    AlgPartition const partition(n);
    SpMatrix<Real> A(partition, 3);
    A.setVal(
        [=] AMREX_GPU_DEVICE (Long row, Long* col, Real* val) noexcept
        {
            col[0] = (row+n-1) % n;
            col[1] = row;
            col[2] = (row+1) % n;
            val[0] = Real(-1);
            val[1] = Real(3.0);
            val[2] = Real(-1);
        },
        CsrSorted{false});

    AlgVector<Real> exact(partition);
    AlgVector<Real> b(partition);
    fill_exact(exact);
    SpMV(b, A, exact);

    AMG<Real> amg(A);
    amg.setup();
    AMREX_ALWAYS_ASSERT(amg.numLevels() > 1);

    AlgVector<Real> correction(partition);
    amg.apply(correction, b);
    Real const initial_residual = b.norm2();
    Real const cycle_residual = true_residual(A, correction, b);
    amrex::Print() << "AMG levels: " << amg.numLevels()
                   << ", one-cycle residual ratio: "
                   << cycle_residual/initial_residual << '\n';
    AMREX_ALWAYS_ASSERT(cycle_residual < initial_residual);

    Real const rtol =
        (sizeof(Real) == sizeof(float)) ? Real(2.e-5) : Real(1.e-10);
    AlgVector<Real> stationary_solution(partition);
    stationary_solution.setVal(Real(0));
    Real const solve_rtol = Real(0.1)*rtol;
    amg.solve(stationary_solution, b, solve_rtol, Real(0), 100);
    Real const stationary_residual =
        true_residual(A, stationary_solution, b);
    amrex::Print() << "Stationary AMG iterations: "
                   << amg.lastIterations()
                   << ", true residual: " << stationary_residual << '\n';
    AMREX_ALWAYS_ASSERT(stationary_residual <= rtol*initial_residual);

    AlgVector<Real> gmres_solution(partition);
    gmres_solution.setVal(Real(0));
    GMRES_MV<Real> gmres(&A);
    gmres.setPrecond(
        [&amg] (AlgVector<Real>& lhs, AlgVector<Real> const& rhs)
        {
            amg.apply(lhs, rhs);
        });
    gmres.solve(gmres_solution, b, rtol, Real(0));
    Real const gmres_residual = true_residual(A, gmres_solution, b);
    amrex::Print() << "AMG-preconditioned GMRES true residual: "
                   << gmres_residual << '\n';
    AMREX_ALWAYS_ASSERT(gmres_residual <= rtol*initial_residual);

#ifdef AMREX_USE_HYPRE
    int hypre_iterations = 0;
    auto hypre_solution =
        solve_with_boomeramg(partition, solve_rtol, 100, hypre_iterations);
    Real const hypre_residual =
        true_residual(A, hypre_solution, b);
    AlgVector<Real> difference(partition);
    difference.copy(stationary_solution);
    Axpy(difference, Real(-1), hypre_solution);
    Real const solution_difference = difference.norm2();
    Real const agreement_limit = rtol*hypre_solution.norm2();
    amrex::Print() << "BoomerAMG iterations: " << hypre_iterations
                   << ", true residual: " << hypre_residual
                   << ", solution difference: " << solution_difference
                   << ", agreement limit: " << agreement_limit << '\n';
    AMREX_ALWAYS_ASSERT(hypre_residual <= rtol*initial_residual);
    AMREX_ALWAYS_ASSERT(solution_difference <= agreement_limit);
#endif

    amrex::Finalize();
}

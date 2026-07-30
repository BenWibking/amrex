#include <AMReX.H>
#include <AMReX_AlgVecUtil.H>
#include <AMReX_SpGEMM.H>
#include <AMReX_SpMatUtil.H>
#include <AMReX_SpMV.H>

using namespace amrex;

namespace {

Real
tolerance ()
{
    return (sizeof(Real) == sizeof(float)) ? Real(3.e-5) : Real(5.e-13);
}

AlgVector<Real>
make_vector (AlgPartition const& partition)
{
    AlgVector<Real> x(partition);
    auto* px = x.data();
    Long const begin = x.globalBegin();
    ParallelFor(x.numLocalRows(),
                [=] AMREX_GPU_DEVICE (Long i) noexcept
    {
        Long const row = i+begin;
        px[i] = Real(1.0) + Real(0.125)*Real(row % 11)
            - Real(0.03125)*Real(row % 3);
    });
    return x;
}

void
assert_canonical (SpMatrix<Real> const& matrix)
{
    auto rows =
        SpGEMMHelper<Real,DefaultAllocator>::copy_local_global_csr(matrix);
    for (Long i = 0; i < rows.nrows(); ++i) {
        Long previous = -1;
        for (Long p = rows.row_offset[i]; p < rows.row_offset[i+1]; ++p) {
            AMREX_ALWAYS_ASSERT(rows.col_index[p] > previous);
            AMREX_ALWAYS_ASSERT(rows.mat[p] != Real(0));
            previous = rows.col_index[p];
        }
    }
}

Real
action_error (SpMatrix<Real> const& actual,
              SpMatrix<Real> const& expected,
              AlgPartition const& column_partition)
{
    auto x = make_vector(column_partition);
    AlgVector<Real> actual_x(actual.partition());
    AlgVector<Real> expected_x(expected.partition());
    SpMV(actual_x, actual, x);
    SpMV(expected_x, expected, x);
    Axpy(actual_x, Real(-1), expected_x);
    return actual_x.norminf();
}

Real
product_action_error (SpMatrix<Real> const& product,
                      SpMatrix<Real> const& A,
                      SpMatrix<Real> const& B,
                      AlgPartition const& column_partition)
{
    auto x = make_vector(column_partition);
    AlgVector<Real> bx(B.partition());
    AlgVector<Real> expected(A.partition());
    AlgVector<Real> actual(A.partition());
    SpMV(bx, B, x);
    SpMV(expected, A, bx);
    SpMV(actual, product, x);
    Axpy(actual, Real(-1), expected);
    return actual.norminf();
}

SpMatrix<Real>
make_identity (AlgPartition const& partition)
{
    SpMatrix<Real> identity(partition, partition, 1);
    identity.setVal(
        [] AMREX_GPU_DEVICE (Long row, Long* col, Real* val) noexcept
        {
            col[0] = row;
            val[0] = Real(1);
        },
        CsrSorted{true});
    return identity;
}

void
test_identity_and_rectangular ()
{
    constexpr Long m = 29;
    constexpr Long k = 23;
    constexpr Long n = 31;
    AlgPartition const m_partition(m);
    AlgPartition const k_partition(k);
    AlgPartition const n_partition(n);

    SpMatrix<Real> A(m_partition, k_partition, 3);
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

    SpMatrix<Real> B(k_partition, n_partition, 3);
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

    auto C = SpGEMM(A, B, n_partition);
    AMREX_ALWAYS_ASSERT(C.partition() == m_partition);
    AMREX_ALWAYS_ASSERT(C.columnPartition() == n_partition);
    AMREX_ALWAYS_ASSERT(
        product_action_error(C, A, B, n_partition) < tolerance());
    assert_canonical(C);

    auto Im = make_identity(m_partition);
    auto Ik = make_identity(k_partition);
    auto left = SpGEMM(Im, A, k_partition);
    auto right = SpGEMM(A, Ik, k_partition);
    AMREX_ALWAYS_ASSERT(action_error(left, A, k_partition) < tolerance());
    AMREX_ALWAYS_ASSERT(action_error(right, A, k_partition) < tolerance());
    assert_canonical(left);
    assert_canonical(right);

    auto CT = transpose(C, n_partition);
    auto AT = transpose(A, k_partition);
    auto BT = transpose(B, n_partition);
    auto BTAT = SpGEMM(BT, AT, m_partition);
    AMREX_ALWAYS_ASSERT(action_error(CT, BTAT, m_partition) < tolerance());
}

void
test_permutation ()
{
    constexpr Long n = 37;
    constexpr Long shift = 5;
    AlgPartition const partition(n);
    SpMatrix<Real> P(partition, partition, 1);
    SpMatrix<Real> Pinverse(partition, partition, 1);
    P.setVal(
        [=] AMREX_GPU_DEVICE (Long row, Long* col, Real* val) noexcept
        {
            col[0] = (row+shift) % n;
            val[0] = Real(1);
        },
        CsrSorted{true});
    Pinverse.setVal(
        [=] AMREX_GPU_DEVICE (Long row, Long* col, Real* val) noexcept
        {
            col[0] = (row+n-shift) % n;
            val[0] = Real(1);
        },
        CsrSorted{true});
    auto product = SpGEMM(P, Pinverse, partition);
    auto identity = make_identity(partition);
    AMREX_ALWAYS_ASSERT(
        action_error(product, identity, partition) < tolerance());
    assert_canonical(product);
}

void
test_laplacian_square ()
{
    constexpr Long n = 41;
    AlgPartition const partition(n);
    SpMatrix<Real> L(partition, partition, 3);
    L.setVal(
        [=] AMREX_GPU_DEVICE (Long row, Long* col, Real* val) noexcept
        {
            col[0] = (row+n-1) % n;
            col[1] = row;
            col[2] = (row+1) % n;
            val[0] = Real(-1);
            val[1] = Real(2);
            val[2] = Real(-1);
        },
        CsrSorted{false});

    SpMatrix<Real> expected(partition, partition, 5);
    expected.setVal(
        [=] AMREX_GPU_DEVICE (Long row, Long* col, Real* val) noexcept
        {
            col[0] = (row+n-2) % n;
            col[1] = (row+n-1) % n;
            col[2] = row;
            col[3] = (row+1) % n;
            col[4] = (row+2) % n;
            val[0] = Real(1);
            val[1] = Real(-4);
            val[2] = Real(6);
            val[3] = Real(-4);
            val[4] = Real(1);
        },
        CsrSorted{false});

    auto square = SpGEMM(L, L, partition);
    AMREX_ALWAYS_ASSERT(
        action_error(square, expected, partition) < tolerance());
    assert_canonical(square);
}

void
test_empty_local_rows_and_remote_import ()
{
    Long const nprocs = ParallelDescriptor::NProcs();
    Long const m = 1;
    Long const k = 2*nprocs+1;
    Long const n = 2;
    AlgPartition const m_partition(m);
    AlgPartition const k_partition(k);
    AlgPartition const n_partition(n);

    SpMatrix<Real> A(m_partition, k_partition, 3);
    A.setVal(
        [=] AMREX_GPU_DEVICE (Long, Long* col, Real* val) noexcept
        {
            col[0] = 0;
            col[1] = k/2;
            col[2] = k-1;
            val[0] = Real(0.5);
            val[1] = Real(-0.25);
            val[2] = Real(1.25);
        },
        CsrSorted{true});
    SpMatrix<Real> B(k_partition, n_partition, 1);
    B.setVal(
        [] AMREX_GPU_DEVICE (Long row, Long* col, Real* val) noexcept
        {
            col[0] = row % 2;
            val[0] = Real(1) + Real(0.125)*Real(row);
        },
        CsrSorted{true});

    auto product = SpGEMM(A, B, n_partition);
    AMREX_ALWAYS_ASSERT(
        product_action_error(product, A, B, n_partition) < tolerance());
    assert_canonical(product);
}

}

int
main (int argc, char* argv[])
{
    amrex::Initialize(argc, argv);

    test_identity_and_rectangular();
    test_permutation();
    test_laplacian_square();
    test_empty_local_rows_and_remote_import();

    amrex::Print()
        << "SpGEMM identity, rectangular, permutation, transpose, "
        << "Laplacian-square, and empty-row tests passed\n";
    amrex::Finalize();
}

#ifndef AMREX_SPMV_H_
#define AMREX_SPMV_H_
#include <AMReX_Config.H>

#include <AMReX_AlgVector.H>
#include <AMReX_AlgVecUtil.H>
#include <AMReX_GpuComplex.H>
#include <AMReX_SpMatrix.H>

namespace amrex {

/**
 * \file AMReX_SpMV.H
 *
 * Back-end implementations of sparse matrix-vector products (CSR).
 */

template <typename T, typename I = Long>
/**
 * \brief Perform y = A * x using CSR data (GPU/CPU aware).
 *
 * With CUDA the product is asynchronous with respect to the host.
 *
 * \param nrows Number of matrix rows.
 * \param ncols Number of matrix columns.
 * \param py    Destination vector (length nrows).
 * \param A     CSR view of the matrix.
 * \param px    Source vector (length ncols).
 */
void SpMV (Long nrows, Long ncols, T* AMREX_RESTRICT py, CsrView<T const,I> const& A,
           T const* AMREX_RESTRICT px
#ifdef AMREX_USE_HIP
           , std::unique_ptr<detail::HipSpMVCache>* hip_cache = nullptr
#endif
           )
{
    T const* AMREX_RESTRICT mat = A.mat;
    auto const* AMREX_RESTRICT col = A.col_index;
    auto const* AMREX_RESTRICT row = A.row_offset;

#if defined(AMREX_USE_GPU)

    Long const nnz = A.nnz;

    // cuSPARSE 12 leaves y untouched when the matrix has no nonzeros.
    if (nnz == 0) {
        ParallelFor(nrows, [=] AMREX_GPU_DEVICE (Long i) { py[i] = T(0); });
        return;
    }

#if defined(AMREX_USE_CUDA)

    cusparseHandle_t handle;
    AMREX_CUSPARSE_SAFE_CALL(cusparseCreate(&handle));
    AMREX_CUSPARSE_SAFE_CALL(cusparseSetStream(handle, Gpu::gpuStream()));

    cudaDataType data_type;
    if constexpr (std::is_same_v<T,float>) {
        data_type = CUDA_R_32F;
    } else if constexpr (std::is_same_v<T,double>) {
        data_type = CUDA_R_64F;
    } else if constexpr (std::is_same_v<T,GpuComplex<float>>) {
        data_type = CUDA_C_32F;
    } else if constexpr (std::is_same_v<T,GpuComplex<double>>) {
        data_type = CUDA_C_64F;
    } else {
        amrex::Abort("SpMV: unsupported data type");
    }

    // cuSPARSE needs the same index type for the row offsets and the
    // column indices (mixed types came with CUDA 13.3).
    constexpr cusparseIndexType_t index_type = std::is_same_v<I,int>
        ? CUSPARSE_INDEX_32I : CUSPARSE_INDEX_64I;

    cusparseSpMatDescr_t mat_descr;
    AMREX_CUSPARSE_SAFE_CALL
        (cusparseCreateCsr(&mat_descr, nrows, ncols, nnz,
                           (void*)row, (void*)col, (void*)mat,
                           index_type, index_type, CUSPARSE_INDEX_BASE_ZERO,
                           data_type));

    cusparseDnVecDescr_t x_descr;
    AMREX_CUSPARSE_SAFE_CALL(cusparseCreateDnVec(&x_descr, ncols, (void*)px, data_type));

    cusparseDnVecDescr_t y_descr;
    AMREX_CUSPARSE_SAFE_CALL(cusparseCreateDnVec(&y_descr, nrows, (void*)py, data_type));

    T alpha = T(1);
    T beta = T(0);

    std::size_t buffer_size;
    AMREX_CUSPARSE_SAFE_CALL
        (cusparseSpMV_bufferSize(handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
                                 &alpha, mat_descr, x_descr, &beta, y_descr,
                                 data_type, CUSPARSE_SPMV_ALG_DEFAULT,
                                 &buffer_size));

    auto* pbuffer = (void*)The_Async_Arena()->alloc(buffer_size);

    AMREX_CUSPARSE_SAFE_CALL
        (cusparseSpMV(handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
                      &alpha, mat_descr, x_descr, &beta, y_descr,
                      data_type, CUSPARSE_SPMV_ALG_DEFAULT, pbuffer));

    AMREX_CUSPARSE_SAFE_CALL(cusparseDestroySpMat(mat_descr));
    AMREX_CUSPARSE_SAFE_CALL(cusparseDestroyDnVec(x_descr));
    // No sync needed: the descriptors are host objects, cusparseDestroy defers
    // the release of GPU resources, and the buffer is freed in stream order.
    AMREX_CUSPARSE_SAFE_CALL(cusparseDestroyDnVec(y_descr));
    AMREX_CUSPARSE_SAFE_CALL(cusparseDestroy(handle));
    The_Async_Arena()->free(pbuffer);

#elif defined(AMREX_USE_HIP)

    rocsparse_datatype data_type;
    if constexpr (std::is_same_v<T,float>) {
        data_type = rocsparse_datatype_f32_r;
    } else if constexpr (std::is_same_v<T,double>) {
        data_type = rocsparse_datatype_f64_r;
    } else if constexpr (std::is_same_v<T,GpuComplex<float>>) {
        data_type = rocsparse_datatype_f32_c;
    } else if constexpr (std::is_same_v<T,GpuComplex<double>>) {
        data_type = rocsparse_datatype_f64_c;
    } else {
        amrex::Abort("SpMV: unsupported data type");
    }

    constexpr rocsparse_indextype index_type = std::is_same_v<I,int>
        ? rocsparse_indextype_i32 : rocsparse_indextype_i64;

    T alpha = T(1);
    T beta = T(0);
    // Standalone CSR views get a temporary cache; distributed matrices pass
    // their own cache so initialization/analysis happens only on first use.
    std::unique_ptr<detail::HipSpMVCache> temporary;
    auto& state = hip_cache ? *hip_cache : temporary;
    if (!state) {
        state = std::make_unique<detail::HipSpMVCache>(
            nrows, ncols, nnz, (void*)row, (void*)col, (void*)mat,
            index_type, data_type, (void*)px, (void*)py, &alpha, &beta);
    }
    state->compute(&alpha, &beta, (void*)px, (void*)py);

#elif defined(AMREX_USE_SYCL)

    mkl::sparse::matrix_handle_t handle{};
    mkl::sparse::init_matrix_handle(&handle);

#if defined(INTEL_MKL_VERSION) && (INTEL_MKL_VERSION < 20250300)
    amrex::ignore_unused(nnz);
    mkl::sparse::set_csr_data(Gpu::Device::streamQueue(), handle, I(nrows), I(ncols),
                              mkl::index_base::zero, (I*)row, (I*)col, (T*)mat);
#else
    mkl::sparse::set_csr_data(Gpu::Device::streamQueue(), handle, I(nrows), I(ncols),
                              I(nnz), mkl::index_base::zero, (I*)row, (I*)col, (T*)mat);
#endif
    mkl::sparse::gemv(Gpu::Device::streamQueue(), mkl::transpose::nontrans,
                      T(1), handle, px, T(0), py);

    auto ev = mkl::sparse::release_matrix_handle(Gpu::Device::streamQueue(), &handle);
    ev.wait();

#endif

    AMREX_GPU_ERROR_CHECK();

#else

    amrex::ignore_unused(ncols);

#ifdef AMREX_USE_OMP
#pragma omp parallel for
#endif
    for (Long i = 0; i < nrows; ++i) {
        T r = 0;
        for (Long j = row[i]; j < row[i+1]; ++j) {
            r += mat[j] * px[col[j]];
        }
        py[i] = r;
    }

#endif
}

/**
 * \brief Perform \p y = \p A * \p x using AlgVector wrappers.
 *
 * \param y Destination vector.
 * \param A Sparse matrix expressed in CSR form.
 * \param x Source vector.
 */
template <typename T, template<typename> class AllocM, typename AllocV>
void SpMV (AlgVector<T,AllocV>& y, SpMatrix<T,AllocM> const& A,
           AlgVector<T,AllocV> const& x)
{
    BL_PROFILE("SpMV");

    auto& Am = const_cast<SpMatrix<T,AllocM>&>(A);
    Am.setColumnPartition(x.partition());
    Am.startComm_mv(x);

    // Diagonal part
    SpMV<T,int>(y.numLocalRows(), x.numLocalRows(), y.data(), A.m_csr_local.const_view(),
                x.data()
#ifdef AMREX_USE_HIP
                , &Am.m_hip_spmv
#endif
                );

    Am.finishComm_mv(y);
}

/**
 * \brief Compute the residual \p res = \p b - \p A * \p x.
 *
 * \param res Output residual vector.
 * \param A   Sparse matrix.
 * \param x   Current solution approximation.
 * \param b   Right-hand side.
 */
template <typename T, template<typename> class AllocM, typename AllocV>
void computeResidual (AlgVector<T,AllocV>& res, SpMatrix<T,AllocM> const& A,
                      AlgVector<T,AllocV> const& x, AlgVector<T,AllocV> const& b)
{
    SpMV(res, A, x);
    Xpay(res, T(-1), b);
}

}

#endif

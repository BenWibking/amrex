#ifndef AMREX_HIP_SPMV_H_
#define AMREX_HIP_SPMV_H_
#include <AMReX_Config.H>

#ifdef AMREX_USE_HIP
#include <AMReX_Gpu.H>
#include <AMReX_BLProfiler.H>
#include <rocsparse/rocsparse.h>

namespace amrex::detail {

// Owned by one split SpMatrix. Neither the CSR storage nor its shape may
// change during this cache's lifetime. Vector storage may change each call.
class HipSpMVCache
{
public:
    HipSpMVCache (Long nrows, Long ncols, Long nnz, void* row, void* col,
                  void* mat, rocsparse_indextype index_type,
                  rocsparse_datatype data_type, void* x, void* y,
                  void const* alpha, void const* beta)
        : m_data_type(data_type)
    {
        BL_PROFILE("SpMV::HIP::initialize");
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_create_handle(&m_handle));
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_set_stream(m_handle, Gpu::gpuStream()));
        AMREX_ROCSPARSE_SAFE_CALL(
            rocsparse_create_csr_descr(&m_mat, nrows, ncols, nnz, row, col, mat,
                                       index_type, index_type,
                                       rocsparse_index_base_zero, data_type));
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_create_dnvec_descr(&m_x, ncols, x, data_type));
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_create_dnvec_descr(&m_y, nrows, y, data_type));

        {
            BL_PROFILE("SpMV::HIP::preprocess");
#if HIP_VERSION_MAJOR >= 7
            AMREX_ROCSPARSE_SAFE_CALL(rocsparse_create_spmv_descr(&m_spmv));
            const rocsparse_spmv_alg alg = rocsparse_spmv_alg_csr_adaptive;
            const rocsparse_operation op = rocsparse_operation_none;
            AMREX_ROCSPARSE_SAFE_CALL(
                rocsparse_spmv_set_input(m_handle, m_spmv, rocsparse_spmv_input_alg,
                                         &alg, sizeof(alg), nullptr));
            AMREX_ROCSPARSE_SAFE_CALL(
                rocsparse_spmv_set_input(m_handle, m_spmv, rocsparse_spmv_input_operation,
                                         &op, sizeof(op), nullptr));
            AMREX_ROCSPARSE_SAFE_CALL(
                rocsparse_spmv_set_input(m_handle, m_spmv, rocsparse_spmv_input_scalar_datatype,
                                         &data_type, sizeof(data_type), nullptr));
            AMREX_ROCSPARSE_SAFE_CALL(
                rocsparse_spmv_set_input(m_handle, m_spmv, rocsparse_spmv_input_compute_datatype,
                                         &data_type, sizeof(data_type), nullptr));
            std::size_t analysis_size = 0;
            AMREX_ROCSPARSE_SAFE_CALL(
                rocsparse_v2_spmv_buffer_size(m_handle, m_spmv, m_mat, m_x, m_y,
                                              rocsparse_v2_spmv_stage_analysis,
                                              &analysis_size, nullptr));
            void* analysis = analysis_size ? The_Arena()->alloc(analysis_size) : nullptr;
            AMREX_ROCSPARSE_SAFE_CALL(
                rocsparse_v2_spmv(m_handle, m_spmv, alpha, m_mat, m_x, beta, m_y,
                                  rocsparse_v2_spmv_stage_analysis,
                                  analysis_size, analysis, nullptr));
            // Analysis scratch is temporary; the descriptor retains the analysis.
            Gpu::streamSynchronize();
            if (analysis) { The_Arena()->free(analysis); }
            AMREX_ROCSPARSE_SAFE_CALL(
                rocsparse_v2_spmv_buffer_size(m_handle, m_spmv, m_mat, m_x, m_y,
                                              rocsparse_v2_spmv_stage_compute,
                                              &m_buffer_size, nullptr));
            if (m_buffer_size) { m_buffer = The_Arena()->alloc(m_buffer_size); }
#elif HIP_VERSION_MAJOR == 6
            AMREX_ROCSPARSE_SAFE_CALL(
                rocsparse_spmv(m_handle, rocsparse_operation_none, alpha, m_mat, m_x,
                               beta, m_y, data_type, rocsparse_spmv_alg_default,
                               rocsparse_spmv_stage_buffer_size, &m_buffer_size, nullptr));
            if (m_buffer_size) { m_buffer = The_Arena()->alloc(m_buffer_size); }
            AMREX_ROCSPARSE_SAFE_CALL(
                rocsparse_spmv(m_handle, rocsparse_operation_none, alpha, m_mat, m_x,
                               beta, m_y, data_type, rocsparse_spmv_alg_default,
                               rocsparse_spmv_stage_preprocess, &m_buffer_size, m_buffer));
#else
            AMREX_ROCSPARSE_SAFE_CALL(
                rocsparse_spmv(m_handle, rocsparse_operation_none, alpha, m_mat, m_x,
                               beta, m_y, data_type, rocsparse_spmv_alg_default,
                               &m_buffer_size, nullptr));
            if (m_buffer_size) { m_buffer = The_Arena()->alloc(m_buffer_size); }
#endif
        }
    }

    HipSpMVCache (HipSpMVCache const&) = delete;
    HipSpMVCache& operator= (HipSpMVCache const&) = delete;

    ~HipSpMVCache ()
    {
        // compute() retains the original blocking HIP SpMV contract, so all
        // device work is complete before matrix moves, replacement or teardown.
#if HIP_VERSION_MAJOR >= 7
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_destroy_spmv_descr(m_spmv));
#endif
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_destroy_spmat_descr(m_mat));
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_destroy_dnvec_descr(m_x));
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_destroy_dnvec_descr(m_y));
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_destroy_handle(m_handle));
        if (m_buffer) { The_Arena()->free(m_buffer); }
    }

    void compute (void const* alpha, void const* beta, void* x, void* y)
    {
        BL_PROFILE("SpMV::HIP::compute");
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_set_stream(m_handle, Gpu::gpuStream()));
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_dnvec_set_values(m_x, x));
        AMREX_ROCSPARSE_SAFE_CALL(rocsparse_dnvec_set_values(m_y, y));
#if HIP_VERSION_MAJOR >= 7
        AMREX_ROCSPARSE_SAFE_CALL(
            rocsparse_v2_spmv(m_handle, m_spmv, alpha, m_mat, m_x, beta, m_y,
                              rocsparse_v2_spmv_stage_compute,
                              m_buffer_size, m_buffer, nullptr));
#elif HIP_VERSION_MAJOR == 6
        AMREX_ROCSPARSE_SAFE_CALL(
            rocsparse_spmv(m_handle, rocsparse_operation_none, alpha, m_mat, m_x,
                           beta, m_y, m_data_type, rocsparse_spmv_alg_default,
                           rocsparse_spmv_stage_compute, &m_buffer_size, m_buffer));
#else
        AMREX_ROCSPARSE_SAFE_CALL(
            rocsparse_spmv(m_handle, rocsparse_operation_none, alpha, m_mat, m_x,
                           beta, m_y, m_data_type, rocsparse_spmv_alg_default,
                           &m_buffer_size, m_buffer));
#endif
        {
            BL_PROFILE("SpMV::HIP::synchronize");
            Gpu::streamSynchronize();
        }
    }

private:
    rocsparse_handle m_handle = nullptr;
    rocsparse_spmat_descr m_mat = nullptr;
    rocsparse_dnvec_descr m_x = nullptr;
    rocsparse_dnvec_descr m_y = nullptr;
#if HIP_VERSION_MAJOR >= 7
    rocsparse_spmv_descr m_spmv = nullptr;
#endif
    rocsparse_datatype m_data_type;
    std::size_t m_buffer_size = 0;
    void* m_buffer = nullptr;
};

} // namespace amrex::detail
#endif
#endif

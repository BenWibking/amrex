#ifndef AMREX_GPU_KERNEL_INFO_H_
#define AMREX_GPU_KERNEL_INFO_H_
#include <AMReX_Config.H>
#include <cstddef>

// Forward declarations for GPU types
#ifdef AMREX_USE_GPU
namespace amrex {
    struct dim3;
    struct gpuStream_t;
}
#endif

namespace amrex::Gpu {

class KernelInfo
{
public:
    KernelInfo& setReduction (bool flag) { has_reduction = flag; return *this; }
    [[nodiscard]] bool hasReduction () const { return has_reduction; }
    
    // Profiling interface
    static bool isProfilingEnabled() { return s_profiling_enabled; }
    static void setProfilingEnabled(bool enabled) { s_profiling_enabled = enabled; }
    
#ifdef AMREX_USE_GPU
    static void startKernel(const char* name, amrex::dim3 numBlocks, amrex::dim3 numThreads, 
                          std::size_t sharedMem, amrex::gpuStream_t stream, 
                          const char* file, int line);
    static void startKernel(const char* name, int numBlocks, int numThreads, 
                          std::size_t sharedMem, amrex::gpuStream_t stream, 
                          const char* file, int line);
#else
    static void startKernel(const char* name, int numBlocks, int numThreads, 
                          std::size_t sharedMem, int stream, 
                          const char* file, int line);
#endif
    static void endKernel();
    
private:
    bool has_reduction = false;
    static bool s_profiling_enabled;
};

}

#endif

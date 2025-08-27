// ABOUTME: Implementation of GPU kernel profiling instrumentation
// ABOUTME: Provides hooks for tracking kernel launch information including file/line, stream, and launch configuration

#include <AMReX_GpuKernelInfo.H>
#include <AMReX_Print.H>
#include <iostream>
#include <iomanip>

namespace amrex::Gpu {

bool KernelInfo::s_profiling_enabled = false;

#ifdef AMREX_USE_GPU
void KernelInfo::startKernel(const char* name, dim3 numBlocks, dim3 numThreads, 
                            std::size_t sharedMem, gpuStream_t stream, 
                            const char* file, int line)
{
    if (s_profiling_enabled) {
        amrex::Print() << "[GPU KERNEL LAUNCH] " << name 
                      << " at " << file << ":" << line
                      << " | Blocks: (" << numBlocks.x << "," << numBlocks.y << "," << numBlocks.z << ")"
                      << " | Threads: (" << numThreads.x << "," << numThreads.y << "," << numThreads.z << ")"
                      << " | SharedMem: " << sharedMem
#ifdef AMREX_USE_SYCL
                      << " | Stream: " << stream.queue
#else
                      << " | Stream: " << stream
#endif
                      << std::endl;
    }
}

void KernelInfo::startKernel(const char* name, int numBlocks, int numThreads, 
                            std::size_t sharedMem, gpuStream_t stream, 
                            const char* file, int line)
{
    if (s_profiling_enabled) {
        amrex::Print() << "[GPU KERNEL LAUNCH] " << name 
                      << " at " << file << ":" << line
                      << " | Blocks: " << numBlocks
                      << " | Threads: " << numThreads
                      << " | SharedMem: " << sharedMem
#ifdef AMREX_USE_SYCL
                      << " | Stream: " << stream.queue
#else
                      << " | Stream: " << stream
#endif
                      << std::endl;
    }
}
#else
void KernelInfo::startKernel(const char* name, int numBlocks, int numThreads, 
                            std::size_t sharedMem, int stream, 
                            const char* file, int line)
{
    if (s_profiling_enabled) {
        amrex::Print() << "[CPU KERNEL LAUNCH] " << name 
                      << " at " << file << ":" << line
                      << " | Blocks: " << numBlocks
                      << " | Threads: " << numThreads
                      << " | SharedMem: " << sharedMem
                      << " | Stream: " << stream
                      << std::endl;
    }
}
#endif

void KernelInfo::endKernel()
{
    // This could be extended to measure kernel execution time
    // For now, it's a placeholder for future profiling extensions
}

}
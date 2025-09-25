#ifndef AMREX_YAFLUXREGISTER_1D_K_H_
#define AMREX_YAFLUXREGISTER_1D_K_H_
#include <AMReX_Config.H>

#include <AMReX_BaseFab.H>

namespace amrex {

template <typename T>
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
void yafluxreg_crseadd (Box const& bx, Array4<T> const& d, Array4<int const> const& flag,
                        Array4<T const> const& fx, T dtdx, int nc) noexcept
{
    auto const lo = amrex::lbound(bx);
    auto const hi = amrex::ubound(bx);

    for (int i = lo.x; i <= hi.x; ++i) {
        if (flag(i,0,0) == amrex_yafluxreg_crse_fine_boundary_cell)
        {
            if (flag(i-1,0,0) == amrex_yafluxreg_fine_cell) {
                for (int n = 0; n < nc; ++n) {
                    d(i,0,0,n) -= dtdx*fx(i,0,0,n);
                }
            }
            if (flag(i+1,0,0) == amrex_yafluxreg_fine_cell) {
                for (int n = 0; n < nc; ++n) {
                    d(i,0,0,n) += dtdx*fx(i+1,0,0,n);
                }
            }
        }
    }
}

template <typename T>
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
void yafluxreg_fineadd (Box const& bx, Array4<T> const& d, Array4<T const> const& f,
                        T dtdx, int nc, int dirside, Dim3 const& rr) noexcept
{
    const auto lo = amrex::lbound(bx);
    const auto hi = amrex::ubound(bx);

    // Accumulate fine-flux contributions deterministically to retain bitwise reproducibility.
    switch (dirside) {
    case 0 :
    {
        for (int n = 0; n < nc; ++n) {
            for (int i = lo.x; i <= hi.x; ++i) {
                const int ii = (i+1)*rr.x;
                d(i,0,0,n) += -dtdx * f(ii,0,0,n);
            }
        }
        break;
    }
    default:
    {
        for (int n = 0; n < nc; ++n) {
            for (int i = lo.x; i <= hi.x; ++i) {
                const int ii = i*rr.x;
                d(i,0,0,n) += dtdx * f(ii,0,0,n);
            }
        }
    }
    }
}

}
#endif

#ifndef AMREX_YAFLUXREGISTER_3D_K_H_
#define AMREX_YAFLUXREGISTER_3D_K_H_
#include <AMReX_Config.H>

#include <AMReX_BaseFab.H>

namespace amrex {

template <typename T>
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
void yafluxreg_crseadd (Box const& bx, Array4<T> const& d, Array4<int const> const& flag,
                        Array4<T const> const& fx,
                        Array4<T const> const& fy,
                        Array4<T const> const& fz,
                        T dtdx, T dtdy, T dtdz, int nc) noexcept
{
    auto const lo = amrex::lbound(bx);
    auto const hi = amrex::ubound(bx);

    for (int k = lo.z; k <= hi.z; ++k) {
    for (int j = lo.y; j <= hi.y; ++j) {
    for (int i = lo.x; i <= hi.x; ++i) {
        if (flag(i,j,k) == amrex_yafluxreg_crse_fine_boundary_cell)
        {
            if (flag(i-1,j,k) == amrex_yafluxreg_fine_cell) {
                for (int n = 0; n < nc; ++n) {
                    d(i,j,k,n) -= dtdx*fx(i,j,k,n);
                }
            }
            if (flag(i+1,j,k) == amrex_yafluxreg_fine_cell) {
                for (int n = 0; n < nc; ++n) {
                    d(i,j,k,n) += dtdx*fx(i+1,j,k,n);
                }
            }

            if (flag(i,j-1,k) == amrex_yafluxreg_fine_cell) {
                for (int n = 0; n < nc; ++n) {
                    d(i,j,k,n) -= dtdy*fy(i,j,k,n);
                }
            }
            if (flag(i,j+1,k) == amrex_yafluxreg_fine_cell) {
                for (int n = 0; n < nc; ++n) {
                    d(i,j,k,n) += dtdy*fy(i,j+1,k,n);
                }
            }

            if (flag(i,j,k-1) == amrex_yafluxreg_fine_cell) {
                for (int n = 0; n < nc; ++n) {
                    d(i,j,k,n) -= dtdz*fz(i,j,k,n);
                }
            }
            if (flag(i,j,k+1) == amrex_yafluxreg_fine_cell) {
                for (int n = 0; n < nc; ++n) {
                    d(i,j,k,n) += dtdz*fz(i,j,k+1,n);
                }
            }
        }
    }}}
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
            for (int k = lo.z; k <= hi.z; ++k) {
                for (int j = lo.y; j <= hi.y; ++j) {
                    for (int i = lo.x; i <= hi.x; ++i) {
                        const int ii = (i+1)*rr.x;
                        T sum = T(0);
                        for (int koff = 0; koff < rr.z; ++koff) {
                            const int kk = k*rr.z + koff;
                            for (int joff = 0; joff < rr.y; ++joff) {
                                const int jj = j*rr.y + joff;
                                sum -= dtdx * f(ii,jj,kk,n);
                            }
                        }
                        d(i,j,k,n) += sum;
                    }
                }
            }
        }
        break;
    }
    case 1 :
    {
        for (int n = 0; n < nc; ++n) {
            for (int k = lo.z; k <= hi.z; ++k) {
                for (int j = lo.y; j <= hi.y; ++j) {
                    for (int i = lo.x; i <= hi.x; ++i) {
                        const int ii = i*rr.x;
                        T sum = T(0);
                        for (int koff = 0; koff < rr.z; ++koff) {
                            const int kk = k*rr.z + koff;
                            for (int joff = 0; joff < rr.y; ++joff) {
                                const int jj = j*rr.y + joff;
                                sum += dtdx * f(ii,jj,kk,n);
                            }
                        }
                        d(i,j,k,n) += sum;
                    }
                }
            }
        }
        break;
    }
    case 2 :
    {
        for (int n = 0; n < nc; ++n) {
            for (int k = lo.z; k <= hi.z; ++k) {
                for (int j = lo.y; j <= hi.y; ++j) {
                    for (int i = lo.x; i <= hi.x; ++i) {
                        const int jj = (j+1)*rr.y;
                        T sum = T(0);
                        for (int koff = 0; koff < rr.z; ++koff) {
                            const int kk = k*rr.z + koff;
                            for (int ioff = 0; ioff < rr.x; ++ioff) {
                                const int ii = i*rr.x + ioff;
                                sum -= dtdx * f(ii,jj,kk,n);
                            }
                        }
                        d(i,j,k,n) += sum;
                    }
                }
            }
        }
        break;
    }
    case 3 :
    {
        for (int n = 0; n < nc; ++n) {
            for (int k = lo.z; k <= hi.z; ++k) {
                for (int j = lo.y; j <= hi.y; ++j) {
                    for (int i = lo.x; i <= hi.x; ++i) {
                        const int jj = j*rr.y;
                        T sum = T(0);
                        for (int koff = 0; koff < rr.z; ++koff) {
                            const int kk = k*rr.z + koff;
                            for (int ioff = 0; ioff < rr.x; ++ioff) {
                                const int ii = i*rr.x + ioff;
                                sum += dtdx * f(ii,jj,kk,n);
                            }
                        }
                        d(i,j,k,n) += sum;
                    }
                }
            }
        }
        break;
    }
    case 4 :
    {
        for (int n = 0; n < nc; ++n) {
            for (int k = lo.z; k <= hi.z; ++k) {
                for (int j = lo.y; j <= hi.y; ++j) {
                    for (int i = lo.x; i <= hi.x; ++i) {
                        const int kk = (k+1)*rr.z;
                        T sum = T(0);
                        for (int joff = 0; joff < rr.y; ++joff) {
                            const int jj = j*rr.y + joff;
                            for (int ioff = 0; ioff < rr.x; ++ioff) {
                                const int ii = i*rr.x + ioff;
                                sum -= dtdx * f(ii,jj,kk,n);
                            }
                        }
                        d(i,j,k,n) += sum;
                    }
                }
            }
        }
        break;
    }
    default:
    {
        for (int n = 0; n < nc; ++n) {
            for (int k = lo.z; k <= hi.z; ++k) {
                for (int j = lo.y; j <= hi.y; ++j) {
                    for (int i = lo.x; i <= hi.x; ++i) {
                        const int kk = k*rr.z;
                        T sum = T(0);
                        for (int joff = 0; joff < rr.y; ++joff) {
                            const int jj = j*rr.y + joff;
                            for (int ioff = 0; ioff < rr.x; ++ioff) {
                                const int ii = i*rr.x + ioff;
                                sum += dtdx * f(ii,jj,kk,n);
                            }
                        }
                        d(i,j,k,n) += sum;
                    }
                }
            }
        }
    }
    }
}

}
#endif

#include <AMReX.H>
#include <AMReX_AMG.H>
#include <AMReX_AlgVecUtil.H>
#include <AMReX_GMRES_MV.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_SpMV.H>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

using namespace amrex;

namespace
{

using Entry = std::pair<Long, Real>;
using Entries = Vector<Entry>;

constexpr int xlo = 0;
constexpr int xhi = 1;
constexpr int ylo = 2;
constexpr int yhi = 3;

enum class BoundaryKind { periodic, reflecting, dirichlet, marshak };

struct BoundaryCondition
{
    BoundaryKind kind = BoundaryKind::reflecting;
    Real value = Real(0);
    Real beta = Real(0);
};

struct Face
{
    Long neighbor = -1;
    Real area = Real(0);
    Real self_distance = Real(0);
    Real neighbor_distance = Real(0);
    int normal_x = 0;
    int normal_y = 0;
    int side = -1;
    BoundaryCondition boundary;
};

struct Cell
{
    Long id = -1;
    int ilo = 0;
    int ihi = 0;
    int jlo = 0;
    int jhi = 0;
    Real x = Real(0);
    Real y = Real(0);
    Real hx = Real(0);
    Real hy = Real(0);
    Real volume = Real(0);
    Vector<Face> faces;
};

struct Mesh
{
    int fine_n = 0;
    Real fine_h = Real(0);
    Vector<Cell> cells;
    Vector<Long> owner;
    std::array<BoundaryCondition, 4> outer_boundary;
    BoundaryCondition hole_boundary;
    bool has_hole = false;
};

struct LinearSolution
{
    Vector<Real> values;
    int iterations = 0;
    Real relative_residual = Real(0);
};

struct SolverSummary
{
    int solves = 0;
    int total_iterations = 0;
    int maximum_iterations = 0;
    int maximum_levels = 0;
    Real maximum_relative_residual = Real(0);
    double setup_seconds = 0.0;
    double maximum_operator_complexity = 0.0;

    [[nodiscard]] Real
    average_iterations () const noexcept
    {
        return (solves > 0) ? Real(total_iterations) / Real(solves) : Real(0);
    }
};

struct GaussianResult
{
    Real relative_l1_error = Real(0);
    Real relative_energy_drift = Real(0);
    Long cells = 0;
    SolverSummary solver;
};

struct CloudResult
{
    Real transmission = Real(0);
    Real balance_error = Real(0);
    Real cloudy_area_relative_error = Real(0);
    Real minimum_energy = Real(0);
    Real maximum_energy = Real(0);
    Real final_picard_change = Real(0);
    int picard_iterations = 0;
    Long mixed_cells = 0;
    Long cells = 0;
    SolverSummary solver;
};

struct FrontResult
{
    Real front_radius = Real(0);
    Real causal_radius = Real(0);
    Real far_excess = Real(0);
    Real maximum_flux_fraction = Real(0);
    Real minimum_energy = Real(0);
    Real maximum_energy = Real(0);
    Real unlimited_far_excess = Real(0);
    Real final_picard_change = Real(0);
    int total_picard_iterations = 0;
    int maximum_picard_iterations = 0;
    Long cells = 0;
    SolverSummary solver;
};

Real
linear_tolerance ()
{
    return (sizeof(Real) == sizeof(float)) ? Real(5.e-5) : Real(2.e-10);
}

BoundaryCondition
periodic_boundary ()
{
    return {BoundaryKind::periodic, Real(0), Real(0)};
}

BoundaryCondition
reflecting_boundary ()
{
    return {BoundaryKind::reflecting, Real(0), Real(0)};
}

BoundaryCondition
dirichlet_boundary (Real value)
{
    return {BoundaryKind::dirichlet, value, Real(0)};
}

BoundaryCondition
marshak_boundary (Real equilibrium_energy, Real beta)
{
    return {BoundaryKind::marshak, equilibrium_energy, beta};
}

template <typename RefinePredicate, typename ActivePredicate>
Mesh
make_mesh (int nbase, int refinement_ratio, RefinePredicate&& refine,
           ActivePredicate&& active,
           std::array<BoundaryCondition, 4> outer_boundary,
           bool has_hole = false, BoundaryCondition hole_boundary = {})
{
    AMREX_ALWAYS_ASSERT(nbase > 1);
    AMREX_ALWAYS_ASSERT(refinement_ratio > 0);

    Mesh mesh;
    mesh.fine_n = nbase * refinement_ratio;
    mesh.fine_h = Real(1) / Real(mesh.fine_n);
    mesh.owner.resize(static_cast<std::size_t>(mesh.fine_n) * mesh.fine_n,
                      Long(-1));
    mesh.outer_boundary = outer_boundary;
    mesh.has_hole = has_hole;
    mesh.hole_boundary = hole_boundary;

    // Represent composite cells on one finest-level integer lattice so every
    // coarse-fine face can be split into conservative subfaces.
    auto owner_index = [&] (int i, int j) -> std::size_t
    { return static_cast<std::size_t>(j) * mesh.fine_n + i; };

    auto add_cell = [&] (int ilo, int ihi, int jlo, int jhi)
    {
        Cell cell;
        cell.id = static_cast<Long>(mesh.cells.size());
        cell.ilo = ilo;
        cell.ihi = ihi;
        cell.jlo = jlo;
        cell.jhi = jhi;
        cell.hx = Real(ihi - ilo) * mesh.fine_h;
        cell.hy = Real(jhi - jlo) * mesh.fine_h;
        cell.x = Real(0.5) * Real(ilo + ihi) * mesh.fine_h;
        cell.y = Real(0.5) * Real(jlo + jhi) * mesh.fine_h;
        cell.volume = cell.hx * cell.hy;
        mesh.cells.push_back(cell);

        for (int j = jlo; j < jhi; ++j) {
            for (int i = ilo; i < ihi; ++i) {
                auto const index = owner_index(i, j);
                AMREX_ALWAYS_ASSERT(mesh.owner[index] == Long(-1));
                mesh.owner[index] = cell.id;
            }
        }
    };

    for (int j = 0; j < nbase; ++j) {
        for (int i = 0; i < nbase; ++i) {
            bool const refined = refinement_ratio > 1 && refine(i, j, nbase);
            if (refined) {
                for (int jj = 0; jj < refinement_ratio; ++jj) {
                    for (int ii = 0; ii < refinement_ratio; ++ii) {
                        int const fi = i * refinement_ratio + ii;
                        int const fj = j * refinement_ratio + jj;
                        Real const x = (Real(fi) + Real(0.5)) * mesh.fine_h;
                        Real const y = (Real(fj) + Real(0.5)) * mesh.fine_h;
                        if (active(x, y)) {
                            add_cell(fi, fi + 1, fj, fj + 1);
                        }
                    }
                }
            } else {
                int const ilo = i * refinement_ratio;
                int const ihi = (i + 1) * refinement_ratio;
                int const jlo = j * refinement_ratio;
                int const jhi = (j + 1) * refinement_ratio;
                Real const x = Real(0.5) * Real(ilo + ihi) * mesh.fine_h;
                Real const y = Real(0.5) * Real(jlo + jhi) * mesh.fine_h;
                if (active(x, y)) {
                    add_cell(ilo, ihi, jlo, jhi);
                }
            }
        }
    }

    auto add_face = [&] (Cell& cell, int ni, int nj, int side, int normal_x,
                         int normal_y, Real area)
    {
        bool outside =
            ni < 0 || ni >= mesh.fine_n || nj < 0 || nj >= mesh.fine_n;
        BoundaryCondition boundary;

        if (outside) {
            boundary = mesh.outer_boundary[side];
            if (boundary.kind == BoundaryKind::periodic) {
                if (ni < 0) {
                    ni += mesh.fine_n;
                } else if (ni >= mesh.fine_n) {
                    ni -= mesh.fine_n;
                }
                if (nj < 0) {
                    nj += mesh.fine_n;
                } else if (nj >= mesh.fine_n) {
                    nj -= mesh.fine_n;
                }
                outside = false;
            }
        }

        Face face;
        face.area = area;
        face.self_distance =
            (normal_x != 0) ? Real(0.5) * cell.hx : Real(0.5) * cell.hy;
        face.normal_x = normal_x;
        face.normal_y = normal_y;
        face.side = side;

        if (!outside) {
            Long const neighbor = mesh.owner[owner_index(ni, nj)];
            if (neighbor >= 0) {
                if (neighbor == cell.id) {
                    return;
                }
                face.neighbor = neighbor;
                auto const& other = mesh.cells[neighbor];
                face.neighbor_distance = (normal_x != 0) ? Real(0.5) * other.hx
                                                         : Real(0.5) * other.hy;
            } else {
                face.boundary =
                    mesh.has_hole ? mesh.hole_boundary : reflecting_boundary();
            }
        } else {
            face.boundary = boundary;
        }
        cell.faces.push_back(face);
    };

    for (auto& cell : mesh.cells) {
        for (int j = cell.jlo; j < cell.jhi; ++j) {
            add_face(cell, cell.ilo - 1, j, xlo, -1, 0, mesh.fine_h);
            add_face(cell, cell.ihi, j, xhi, 1, 0, mesh.fine_h);
        }
        for (int i = cell.ilo; i < cell.ihi; ++i) {
            add_face(cell, i, cell.jlo - 1, ylo, 0, -1, mesh.fine_h);
            add_face(cell, i, cell.jhi, yhi, 0, 1, mesh.fine_h);
        }
    }

    AMREX_ALWAYS_ASSERT(mesh.cells.size() > 9);
    return mesh;
}

template <typename F>
SpMatrix<Real>
make_matrix (AlgPartition const& partition, F&& make_row)
{
    using host_csr_type = CSR<Real, Gpu::PinnedVector>;
    using csr_type = SpMatrix<Real>::csr_type;

    int const myproc = ParallelDescriptor::MyProc();
    Long const begin = partition[myproc];
    Long const end = partition[myproc + 1];
    Long const nlocal = end - begin;
    host_csr_type host;
    host.row_offset.resize(nlocal + 1);
    host.row_offset[0] = 0;

    for (Long i = 0; i < nlocal; ++i) {
        Entries entries = make_row(begin + i);
        std::sort(entries.begin(), entries.end(),
                  [] (Entry const& lhs, Entry const& rhs)
                  { return lhs.first < rhs.first; });
        for (std::size_t p = 0; p < entries.size();) {
            Long const column = entries[p].first;
            Real value = Real(0);
            do {
                value += entries[p].second;
                ++p;
            } while (p < entries.size() && entries[p].first == column);
            AMREX_ALWAYS_ASSERT(column >= 0 &&
                                column < partition.numGlobalRows());
            if (value != Real(0)) {
                host.col_index.push_back(column);
                host.mat.push_back(value);
            }
        }
        host.row_offset[i + 1] = static_cast<Long>(host.mat.size());
    }
    host.nnz = static_cast<Long>(host.mat.size());

    csr_type device;
    duplicateCSR(Gpu::hostToDevice, device, host);
    Gpu::streamSynchronize();
    return SpMatrix<Real>(partition, partition, std::move(device));
}

AlgVector<Real>
make_vector (AlgPartition const& partition, Vector<Real> const& values)
{
    AMREX_ALWAYS_ASSERT(static_cast<Long>(values.size()) ==
                        partition.numGlobalRows());
    AlgVector<Real> result(partition);
    Long const begin = result.globalBegin();
    Gpu::PinnedVector<Real> local(result.numLocalRows());
    for (Long i = 0; i < result.numLocalRows(); ++i) {
        local[i] = values[begin + i];
    }
    if (!local.empty()) {
        Gpu::copyAsync(Gpu::hostToDevice, local.begin(), local.end(),
                       result.data());
        Gpu::streamSynchronize();
    }
    return result;
}

Vector<Real>
gather_vector (AlgVector<Real> const& vector)
{
    Long const nlocal = vector.numLocalRows();
    Gpu::PinnedVector<Real> local(nlocal);
    if (nlocal > 0) {
        Gpu::copyAsync(Gpu::deviceToHost, vector.data(), vector.data() + nlocal,
                       local.begin());
        Gpu::streamSynchronize();
    }

    auto const& partition = vector.partition();
    int const nprocs = ParallelDescriptor::NProcs();
    std::vector<int> counts(nprocs);
    std::vector<int> offsets(nprocs);
    for (int rank = 0; rank < nprocs; ++rank) {
        Long const count = partition[rank + 1] - partition[rank];
        AMREX_ALWAYS_ASSERT(count <= std::numeric_limits<int>::max());
        AMREX_ALWAYS_ASSERT(partition[rank] <= std::numeric_limits<int>::max());
        counts[rank] = static_cast<int>(count);
        offsets[rank] = static_cast<int>(partition[rank]);
    }

    Vector<Real> global(partition.numGlobalRows());
    ParallelDescriptor::Gatherv(local.data(), static_cast<int>(nlocal),
                                global.data(), counts, offsets,
                                ParallelDescriptor::IOProcessorNumber());
    ParallelDescriptor::Bcast(global.data(), global.size(),
                              ParallelDescriptor::IOProcessorNumber());
    return global;
}

Real
true_relative_residual (SpMatrix<Real> const& matrix,
                        AlgVector<Real> const& solution,
                        AlgVector<Real> const& rhs)
{
    AlgVector<Real> residual(rhs.partition());
    SpMV(residual, matrix, solution);
    LinComb(residual, Real(1), rhs, Real(-1), residual);
    Real const denominator = amrex::max(rhs.norm2(), Real(1.e-30));
    return residual.norm2() / denominator;
}

class AMGGMRESSolver
{
  public:
    explicit AMGGMRESSolver (SpMatrix<Real> const& matrix,
                             AMG<Real>::Options options = {})
        : m_matrix(matrix), m_amg(matrix, options), m_gmres(&matrix)
    {
        m_amg.setup();
        m_gmres.setPrecond(
            [this] (AlgVector<Real>& lhs, AlgVector<Real> const& rhs)
            { m_amg.apply(lhs, rhs); });
        m_gmres.getGMRES().setRestartLength(50);
        m_gmres.getGMRES().setMaxIters(500);
    }

    AMGGMRESSolver (AMGGMRESSolver const&) = delete;
    AMGGMRESSolver& operator=(AMGGMRESSolver const&) = delete;

    [[nodiscard]] LinearSolution
    solve (Vector<Real> const& rhs_values)
    {
        AlgVector<Real> rhs = make_vector(m_matrix.partition(), rhs_values);
        AlgVector<Real> solution(m_matrix.partition());
        solution.setVal(Real(0));
        m_gmres.solve(solution, rhs, linear_tolerance(), Real(0));

        auto const& gmres = m_gmres.getGMRES();
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            gmres.getStatus() == 0,
            "GMRES+AMG did not converge in an FLD regression");

        LinearSolution result;
        result.values = gather_vector(solution);
        result.iterations = gmres.getNumIters();
        result.relative_residual =
            true_relative_residual(m_matrix, solution, rhs);
        AMREX_ALWAYS_ASSERT(result.relative_residual <=
                            Real(5) * linear_tolerance());
        return result;
    }

    [[nodiscard]] AMG<Real>::Diagnostics const&
    diagnostics () const noexcept
    {
        return m_amg.diagnostics();
    }

  private:
    SpMatrix<Real> const& m_matrix;
    AMG<Real> m_amg;
    GMRES_MV<Real> m_gmres;
};

void
record_setup (SolverSummary& summary, AMGGMRESSolver const& solver)
{
    auto const& diagnostics = solver.diagnostics();
    summary.setup_seconds += diagnostics.setup_seconds;
    summary.maximum_levels = amrex::max(
        summary.maximum_levels, static_cast<int>(diagnostics.levels.size()));
    summary.maximum_operator_complexity = amrex::max(
        summary.maximum_operator_complexity, diagnostics.operator_complexity);
}

void
record_solve (SolverSummary& summary, LinearSolution const& solution)
{
    ++summary.solves;
    summary.total_iterations += solution.iterations;
    summary.maximum_iterations =
        amrex::max(summary.maximum_iterations, solution.iterations);
    summary.maximum_relative_residual = amrex::max(
        summary.maximum_relative_residual, solution.relative_residual);
}

Real
face_transmissibility (Face const& face, Long row,
                       Vector<Real> const& diffusion,
                       Vector<Real> const* rosseland_extinction = nullptr)
{
    Real const dself = diffusion[row];
    AMREX_ALWAYS_ASSERT(dself > Real(0));
    if (face.neighbor >= 0) {
        Real const dother = diffusion[face.neighbor];
        AMREX_ALWAYS_ASSERT(dother > Real(0));

        if (rosseland_extinction != nullptr) {
            auto const& extinction = *rosseland_extinction;
            AMREX_ALWAYS_ASSERT(extinction.size() == diffusion.size());
            Real const chi_self = extinction[row];
            Real const chi_other = extinction[face.neighbor];
            AMREX_ALWAYS_ASSERT(chi_self > Real(0));
            AMREX_ALWAYS_ASSERT(chi_other > Real(0));

            // Howell & Greenough Fig. 4: average the Rosseland extinction to
            // the face using the surface-flux formula.  The limiter remains
            // cell centered, so average lambda = D chi in series separately.
            Real const center_distance =
                face.self_distance + face.neighbor_distance;
            Real const arithmetic_mean =
                Real(0.5) * (chi_self + chi_other);
            Real const harmonic_mean =
                Real(2) * chi_self * chi_other / (chi_self + chi_other);
            Real const surface_scale =
                Real(4) / (Real(3) * center_distance);
            Real const face_extinction =
                amrex::min(arithmetic_mean,
                           amrex::max(harmonic_mean, surface_scale));
            Real const lambda_self = dself * chi_self;
            Real const lambda_other = dother * chi_other;
            // Use the less restrictive of the two cell-centered limiters at
            // the face with the Howell-Greenough surface opacity.
            Real const face_limiter =
                amrex::max(lambda_self, lambda_other);
            return face.area * face_limiter /
                   (center_distance * face_extinction);
        }

        // The two half-cell diffusion resistances are in series.  This gives
        // the harmonic face coefficient and preserves flux across AMR
        // interfaces.
        return face.area /
               (face.self_distance / dself + face.neighbor_distance / dother);
    }

    switch (face.boundary.kind) {
    case BoundaryKind::reflecting:
        return Real(0);
    case BoundaryKind::dirichlet:
        return face.area * dself / face.self_distance;
    case BoundaryKind::marshak:
        AMREX_ALWAYS_ASSERT(face.boundary.beta > Real(0));
        return face.area /
               (face.self_distance / dself + Real(1) / face.boundary.beta);
    case BoundaryKind::periodic:
        amrex::Abort("Unresolved periodic FLD boundary face");
        return Real(0);
    }
    return Real(0);
}

struct LinearSystem
{
    SpMatrix<Real> matrix;
    Vector<Real> rhs;
};

LinearSystem
assemble_system (Mesh const& mesh, Vector<Real> const& diffusion,
                 Vector<Real> const& old_state, Real dt, bool transient,
                 Vector<Real> const* rosseland_extinction = nullptr)
{
    Long const n = static_cast<Long>(mesh.cells.size());
    AMREX_ALWAYS_ASSERT(static_cast<Long>(diffusion.size()) == n);
    if (rosseland_extinction != nullptr) {
        AMREX_ALWAYS_ASSERT(
            static_cast<Long>(rosseland_extinction->size()) == n);
    }
    if (transient) {
        AMREX_ALWAYS_ASSERT(static_cast<Long>(old_state.size()) == n);
        AMREX_ALWAYS_ASSERT(dt > Real(0));
    }

    Vector<Entries> rows(n);
    Vector<Real> rhs(n, Real(0));
    for (Long row = 0; row < n; ++row) {
        auto const& cell = mesh.cells[row];
        Real diagonal = transient ? Real(1) : Real(0);
        rhs[row] = transient ? old_state[row] : Real(0);
        Real const scale = (transient ? dt : Real(1)) / cell.volume;

        for (auto const& face : cell.faces) {
            Real const transmissibility =
                face_transmissibility(face, row, diffusion,
                                      rosseland_extinction);
            if (transmissibility == Real(0)) {
                continue;
            }
            Real const coefficient = scale * transmissibility;
            diagonal += coefficient;
            if (face.neighbor >= 0) {
                rows[row].emplace_back(face.neighbor, -coefficient);
            } else {
                rhs[row] += coefficient * face.boundary.value;
            }
        }
        AMREX_ALWAYS_ASSERT(diagonal > Real(0));
        rows[row].emplace_back(row, diagonal);
    }

    AlgPartition partition(n);
    auto matrix =
        make_matrix(partition, [&] (Long row) -> Entries { return rows[row]; });
    return {std::move(matrix), std::move(rhs)};
}

Vector<std::array<Real, 2>>
cell_gradients (Mesh const& mesh, Vector<Real> const& energy)
{
    AMREX_ALWAYS_ASSERT(energy.size() == mesh.cells.size());
    Vector<std::array<Real, 2>> gradient(mesh.cells.size(), {Real(0), Real(0)});

    for (Long row = 0; row < static_cast<Long>(mesh.cells.size()); ++row) {
        auto const& cell = mesh.cells[row];
        for (auto const& face : cell.faces) {
            Real face_energy = energy[row];
            if (face.neighbor >= 0) {
                face_energy = (face.neighbor_distance * energy[row] +
                               face.self_distance * energy[face.neighbor]) /
                              (face.self_distance + face.neighbor_distance);
            } else if (face.boundary.kind == BoundaryKind::dirichlet ||
                       face.boundary.kind == BoundaryKind::marshak) {
                face_energy = face.boundary.value;
            }
            gradient[row][0] +=
                Real(face.normal_x) * face.area * face_energy / cell.volume;
            gradient[row][1] +=
                Real(face.normal_y) * face.area * face_energy / cell.volume;
        }
    }
    return gradient;
}

Real
levermore_pomraning_limiter (Real r)
{
    AMREX_ALWAYS_ASSERT(r >= Real(0));
    if (r > Real(1.e8)) {
        return Real(1) / r;
    }
    return (Real(2) + r) / (Real(6) + Real(3) * r + r * r);
}

Vector<Real>
compute_diffusion (Mesh const& mesh, Vector<Real> const& energy,
                   Vector<Real> const& extinction, bool limited,
                   Real* maximum_flux_fraction = nullptr)
{
    AMREX_ALWAYS_ASSERT(energy.size() == mesh.cells.size());
    AMREX_ALWAYS_ASSERT(extinction.size() == mesh.cells.size());
    auto const gradient = cell_gradients(mesh, energy);
    Vector<Real> diffusion(mesh.cells.size());
    Real maximum_fraction = Real(0);

    for (Long row = 0; row < static_cast<Long>(mesh.cells.size()); ++row) {
        Real const chi = extinction[row];
        Real const e = amrex::max(energy[row], Real(1.e-30));
        AMREX_ALWAYS_ASSERT(chi > Real(0));
        Real const magnitude = std::hypot(gradient[row][0], gradient[row][1]);
        Real const r = magnitude / (chi * e);
        Real const lambda =
            limited ? levermore_pomraning_limiter(r) : Real(1) / Real(3);
        // Radiation speed is normalized to one, so lambda*R is |F|/(c E).
        diffusion[row] = lambda / chi;
        maximum_fraction = amrex::max(maximum_fraction, lambda * r);
    }
    if (maximum_flux_fraction != nullptr) {
        *maximum_flux_fraction = maximum_fraction;
    }
    return diffusion;
}

std::pair<Real, Real>
minimum_maximum (Vector<Real> const& values)
{
    auto const [minimum, maximum] =
        std::minmax_element(values.begin(), values.end());
    return {*minimum, *maximum};
}

Real
maximum_relative_change (Vector<Real> const& lhs, Vector<Real> const& rhs)
{
    AMREX_ALWAYS_ASSERT(lhs.size() == rhs.size());
    Real result = Real(0);
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        Real const scale = amrex::max(
            amrex::max(std::abs(lhs[i]), std::abs(rhs[i])), Real(1.e-12));
        result = amrex::max(result, std::abs(lhs[i] - rhs[i]) / scale);
    }
    return result;
}

GaussianResult
run_gaussian (bool use_amr)
{
    auto periodic = periodic_boundary();
    std::array<BoundaryCondition, 4> boundary{periodic, periodic, periodic,
                                              periodic};

    int const nbase = use_amr ? 32 : 64;
    int const ratio = use_amr ? 2 : 1;
    Mesh mesh = make_mesh(
        nbase, ratio, [] (int i, int j, int n) noexcept
        { return i >= n / 4 && i < 3 * n / 4 && j >= n / 4 && j < 3 * n / 4; },
        [] (Real, Real) noexcept { return true; }, boundary);

    Real constexpr extinction_value = Real(100);
    Real const diffusion_value = Real(1) / (Real(3) * extinction_value);
    Real constexpr initial_time = Real(0.4);
    Real constexpr dt = Real(0.005);
    int constexpr steps = 10;
    Real constexpr pi = Real(3.1415926535897932384626433832795);

    auto exact = [&] (Cell const& cell, Real time) -> Real
    {
        Real const x = cell.x - Real(0.5);
        Real const y = cell.y - Real(0.5);
        Real const radius_squared = x * x + y * y;
        return std::exp(-radius_squared / (Real(4) * diffusion_value * time)) /
               (Real(4) * pi * diffusion_value * time);
    };

    Vector<Real> state(mesh.cells.size());
    Vector<Real> extinction(mesh.cells.size(), extinction_value);
    for (Long row = 0; row < static_cast<Long>(mesh.cells.size()); ++row) {
        state[row] = exact(mesh.cells[row], initial_time);
    }
    Real const initial_energy = std::inner_product(
        state.begin(), state.end(), mesh.cells.begin(), Real(0), std::plus<>(),
        [] (Real energy, Cell const& cell) { return energy * cell.volume; });

    auto diffusion = compute_diffusion(mesh, state, extinction, false);
    auto system = assemble_system(mesh, diffusion, state, dt, true);
    AMGGMRESSolver solver(system.matrix);
    GaussianResult result;
    result.cells = static_cast<Long>(mesh.cells.size());
    record_setup(result.solver, solver);

    for (int step = 0; step < steps; ++step) {
        auto solution = solver.solve(state);
        record_solve(result.solver, solution);
        state = std::move(solution.values);
    }

    Real const final_time = initial_time + Real(steps) * dt;
    Real absolute_error = Real(0);
    Real exact_norm = Real(0);
    Real final_energy = Real(0);
    for (Long row = 0; row < static_cast<Long>(mesh.cells.size()); ++row) {
        Real const reference = exact(mesh.cells[row], final_time);
        Real const volume = mesh.cells[row].volume;
        absolute_error += volume * std::abs(state[row] - reference);
        exact_norm += volume * std::abs(reference);
        final_energy += volume * state[row];
    }
    result.relative_l1_error = absolute_error / exact_norm;
    result.relative_energy_drift =
        std::abs(final_energy - initial_energy) / initial_energy;

    Real const error_limit =
        (sizeof(Real) == sizeof(float)) ? Real(0.12) : Real(0.06);
    Real const conservation_limit =
        (sizeof(Real) == sizeof(float)) ? Real(2.e-3) : Real(2.e-7);
    AMREX_ALWAYS_ASSERT(result.relative_l1_error < error_limit);
    AMREX_ALWAYS_ASSERT(result.relative_energy_drift < conservation_limit);
    return result;
}

Real
semicircle_area_primitive (Real x, Real center, Real radius)
{
    Real const offset = std::clamp(x - center, -radius, radius);
    Real const height =
        std::sqrt(amrex::max(Real(0), radius * radius - offset * offset));
    return Real(0.5) *
           (offset * height +
            radius * radius * std::asin(offset / radius));
}

Real
circle_rectangle_intersection_area (Real center_x, Real center_y, Real radius,
                                    Real xlo, Real xhi, Real ylo, Real yhi)
{
    AMREX_ALWAYS_ASSERT(radius > Real(0));
    AMREX_ALWAYS_ASSERT(xlo < xhi && ylo < yhi);

    Real const nearest_x = std::clamp(center_x, xlo, xhi);
    Real const nearest_y = std::clamp(center_y, ylo, yhi);
    Real const nearest_dx = nearest_x - center_x;
    Real const nearest_dy = nearest_y - center_y;
    if (nearest_dx * nearest_dx + nearest_dy * nearest_dy >=
        radius * radius) {
        return Real(0);
    }

    bool rectangle_inside_circle = true;
    for (Real const x : {xlo, xhi}) {
        for (Real const y : {ylo, yhi}) {
            Real const dx = x - center_x;
            Real const dy = y - center_y;
            rectangle_inside_circle =
                rectangle_inside_circle &&
                dx * dx + dy * dy <= radius * radius;
        }
    }
    Real const rectangle_area = (xhi - xlo) * (yhi - ylo);
    if (rectangle_inside_circle) {
        return rectangle_area;
    }

    Real const integration_lo = amrex::max(xlo, center_x - radius);
    Real const integration_hi = amrex::min(xhi, center_x + radius);
    if (integration_lo >= integration_hi) {
        return Real(0);
    }

    Vector<Real> breakpoints{integration_lo, integration_hi};
    for (Real const y : {ylo, yhi}) {
        Real const offset = std::abs(y - center_y);
        if (offset < radius) {
            Real const dx =
                std::sqrt(radius * radius - offset * offset);
            Real const left = center_x - dx;
            Real const right = center_x + dx;
            if (left > integration_lo && left < integration_hi) {
                breakpoints.push_back(left);
            }
            if (right > integration_lo && right < integration_hi) {
                breakpoints.push_back(right);
            }
        }
    }
    std::sort(breakpoints.begin(), breakpoints.end());

    Real area = Real(0);
    for (std::size_t segment = 1; segment < breakpoints.size(); ++segment) {
        Real const a = breakpoints[segment - 1];
        Real const b = breakpoints[segment];
        if (a >= b) {
            continue;
        }

        Real const midpoint = Real(0.5) * (a + b);
        Real const midpoint_dx = midpoint - center_x;
        Real const half_height = std::sqrt(amrex::max(
            Real(0), radius * radius - midpoint_dx * midpoint_dx));
        Real const circle_lo = center_y - half_height;
        Real const circle_hi = center_y + half_height;
        if (circle_hi <= ylo || circle_lo >= yhi) {
            continue;
        }

        bool const upper_is_circle = circle_hi < yhi;
        bool const lower_is_circle = circle_lo > ylo;
        Real const constant =
            (upper_is_circle ? center_y : yhi) -
            (lower_is_circle ? center_y : ylo);
        int const semicircle_coefficient =
            int(upper_is_circle) + int(lower_is_circle);
        area += constant * (b - a) +
                Real(semicircle_coefficient) *
                    (semicircle_area_primitive(b, center_x, radius) -
                     semicircle_area_primitive(a, center_x, radius));
    }
    return std::clamp(area, Real(0), rectangle_area);
}

Real
cloud_radius ()
{
    Real constexpr spacing = Real(1) / Real(8.5);
    Real constexpr diameter = spacing / Real(1.1);
    return Real(0.5) * diameter;
}

Real
cloud_volume_fraction (Cell const& cell)
{
    Real constexpr spacing = Real(1) / Real(8.5);
    Real const radius = cloud_radius();
    Real const xlo = cell.x - Real(0.5) * cell.hx;
    Real const xhi = cell.x + Real(0.5) * cell.hx;
    Real const ylo = cell.y - Real(0.5) * cell.hy;
    Real const yhi = cell.y + Real(0.5) * cell.hy;
    Real cloudy_area = Real(0);
    for (int cloud = 0; cloud <= 8; ++cloud) {
        Real const center_x = Real(cloud) * spacing;
        cloudy_area += circle_rectangle_intersection_area(
            center_x, Real(0.5), radius, xlo, xhi, ylo, yhi);
    }
    Real const fraction = cloudy_area / cell.volume;
    AMREX_ALWAYS_ASSERT(fraction >= Real(-1.e-12));
    AMREX_ALWAYS_ASSERT(fraction <= Real(1) + Real(1.e-12));
    return std::clamp(fraction, Real(0), Real(1));
}

Mesh
make_cloud_mesh (bool use_amr)
{
    Real constexpr beta = Real(0.5);
    std::array<BoundaryCondition, 4> boundary{
        reflecting_boundary(), reflecting_boundary(),
        marshak_boundary(Real(0), beta), marshak_boundary(Real(4), beta)};

    int const nbase = use_amr ? 32 : 128;
    int const ratio = use_amr ? 4 : 1;
    return make_mesh(
        nbase, ratio, [] (int, int j, int n) noexcept
        { return j >= 3 * n / 8 && j < 5 * n / 8; },
        [] (Real, Real) noexcept { return true; }, boundary);
}

std::pair<Real, Real>
cloud_boundary_fluxes (Mesh const& mesh, Vector<Real> const& energy,
                       Vector<Real> const& diffusion)
{
    Real bottom_flux = Real(0);
    Real top_flux = Real(0);
    for (Long row = 0; row < static_cast<Long>(mesh.cells.size()); ++row) {
        for (auto const& face : mesh.cells[row].faces) {
            if (face.neighbor >= 0 ||
                face.boundary.kind != BoundaryKind::marshak) {
                continue;
            }
            Real const flux = face_transmissibility(face, row, diffusion) *
                              (energy[row] - face.boundary.value);
            if (face.side == ylo) {
                bottom_flux += flux;
            } else if (face.side == yhi) {
                top_flux += flux;
            }
        }
    }
    return {bottom_flux, top_flux};
}

CloudResult
run_cloud (bool use_amr)
{
    Mesh mesh = make_cloud_mesh(use_amr);
    Vector<Real> extinction(mesh.cells.size());
    Vector<Real> state(mesh.cells.size());
    Real cloudy_area = Real(0);
    Long mixed_cells = 0;
    Real constexpr clear_extinction = Real(0.1);
    Real constexpr cloudy_extinction = Real(1000);
    Real constexpr clear_density = Real(1);
    Real constexpr cloudy_density = Real(1);
    for (Long row = 0; row < static_cast<Long>(mesh.cells.size()); ++row) {
        auto const& cell = mesh.cells[row];
        Real const cloudy_fraction = cloud_volume_fraction(cell);
        // Apply the mass-weighted arithmetic Rosseland mean in Fig. 3.  The
        // pure-radiation cloud test specifies no material density contrast,
        // so both densities are normalized to one.
        Real const clear_fraction = Real(1) - cloudy_fraction;
        extinction[row] =
            (cloudy_fraction * cloudy_density * cloudy_extinction +
             clear_fraction * clear_density * clear_extinction) /
            (cloudy_fraction * cloudy_density +
             clear_fraction * clear_density);
        state[row] = Real(0.25) + Real(3.5) * cell.y;
        cloudy_area += cloudy_fraction * cell.volume;
        if (cloudy_fraction > Real(0) && cloudy_fraction < Real(1)) {
            ++mixed_cells;
        }
    }

    CloudResult result;
    result.cells = static_cast<Long>(mesh.cells.size());
    result.mixed_cells = mixed_cells;
    Real constexpr pi = Real(3.1415926535897932384626433832795);
    Real const expected_cloudy_area =
        Real(8.5) * pi * cloud_radius() * cloud_radius();
    result.cloudy_area_relative_error =
        std::abs(cloudy_area - expected_cloudy_area) / expected_cloudy_area;
    Real const cloudy_area_tolerance =
        (sizeof(Real) == sizeof(float)) ? Real(2.e-4) : Real(2.e-12);
    AMREX_ALWAYS_ASSERT(result.cloudy_area_relative_error <
                        cloudy_area_tolerance);
    AMREX_ALWAYS_ASSERT(result.mixed_cells > 0);
    Real constexpr relaxation = Real(0.7);
    Real const picard_tolerance =
        (sizeof(Real) == sizeof(float)) ? Real(2.e-4) : Real(2.e-6);
    int constexpr maximum_picard_iterations = 125;
    AMG<Real>::Options cloud_amg_options;
    cloud_amg_options.priority_seed = 1;
    auto const& incident_boundary = mesh.outer_boundary[yhi];
    AMREX_ALWAYS_ASSERT(incident_boundary.kind == BoundaryKind::marshak);
    Real const incident_marshak_flux =
        Real(0.5) * incident_boundary.beta * incident_boundary.value;
    AMREX_ALWAYS_ASSERT(incident_marshak_flux > Real(0));

    for (int iteration = 0; iteration < maximum_picard_iterations;
         ++iteration) {
        auto diffusion = compute_diffusion(mesh, state, extinction, true);
        auto system = assemble_system(mesh, diffusion, {}, Real(0), false,
                                      &extinction);
        AMGGMRESSolver solver(system.matrix, cloud_amg_options);
        record_setup(result.solver, solver);
        auto solution = solver.solve(system.rhs);
        record_solve(result.solver, solution);

        result.final_picard_change =
            maximum_relative_change(solution.values, state);
        ++result.picard_iterations;
        auto const [iteration_bottom_flux, iteration_top_flux] =
            cloud_boundary_fluxes(mesh, solution.values, diffusion);
        amrex::ignore_unused(iteration_top_flux);
        amrex::Print() << "FLD cloud " << (use_amr ? "AMR" : "uniform")
                       << " Picard iteration=" << result.picard_iterations
                       << ", change=" << result.final_picard_change
                       << ", transmission="
                       << iteration_bottom_flux / incident_marshak_flux
                       << ", GMRES iterations=" << solution.iterations
                       << ", true relative residual="
                       << solution.relative_residual << std::endl;
        if (result.final_picard_change <= picard_tolerance) {
            state = std::move(solution.values);
            break;
        }
        for (std::size_t i = 0; i < state.size(); ++i) {
            state[i] = relaxation * solution.values[i] +
                       (Real(1) - relaxation) * state[i];
        }
    }

    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
        result.final_picard_change <= picard_tolerance,
        "The cloud-layer FLD Picard iteration did not converge");

    auto diffusion = compute_diffusion(mesh, state, extinction, true);
    auto const [bottom_flux, top_flux] =
        cloud_boundary_fluxes(mesh, state, diffusion);
    // beta * (E - E_eq) = c E / 2 - 2 F_inc, so
    // F_inc = beta * E_eq / 2 when beta = c / 2.
    result.transmission = bottom_flux / incident_marshak_flux;
    result.balance_error = std::abs(bottom_flux + top_flux) /
                           amrex::max(std::abs(bottom_flux), Real(1.e-30));
    auto const [minimum, maximum] = minimum_maximum(state);
    result.minimum_energy = minimum;
    result.maximum_energy = maximum;

    Real const balance_limit =
        (sizeof(Real) == sizeof(float)) ? Real(2.e-3) : Real(2.e-6);
    AMREX_ALWAYS_ASSERT(result.balance_error < balance_limit);
    AMREX_ALWAYS_ASSERT(result.minimum_energy >= Real(-1.e-8));
    AMREX_ALWAYS_ASSERT(result.maximum_energy <= Real(4.01));
    AMREX_ALWAYS_ASSERT(result.transmission > Real(0));
    AMREX_ALWAYS_ASSERT(result.transmission < Real(1));
    return result;
}

Mesh
make_front_mesh ()
{
    std::array<BoundaryCondition, 4> boundary{
        reflecting_boundary(), reflecting_boundary(), reflecting_boundary(),
        reflecting_boundary()};
    Real constexpr inner_radius = Real(0.1);
    return make_mesh(
        64, 1, [] (int, int, int) noexcept { return false; },
        [] (Real x, Real y) noexcept
        {
            Real const dx = x - Real(0.5);
            Real const dy = y - Real(0.5);
            return dx * dx + dy * dy > inner_radius * inner_radius;
        },
        boundary, true, dirichlet_boundary(Real(1)));
}

Real
far_excess (Mesh const& mesh, Vector<Real> const& state, Real radius,
            Real ambient_energy)
{
    Real result = Real(0);
    for (Long row = 0; row < static_cast<Long>(mesh.cells.size()); ++row) {
        Real const dx = mesh.cells[row].x - Real(0.5);
        Real const dy = mesh.cells[row].y - Real(0.5);
        if (std::hypot(dx, dy) > radius) {
            result = amrex::max(result, state[row] - ambient_energy);
        }
    }
    return result;
}

Real
front_radius (Mesh const& mesh, Vector<Real> const& state)
{
    Real result = Real(0.1);
    Real constexpr threshold = Real(0.01);
    for (Long row = 0; row < static_cast<Long>(mesh.cells.size()); ++row) {
        if (state[row] > threshold) {
            Real const dx = mesh.cells[row].x - Real(0.5);
            Real const dy = mesh.cells[row].y - Real(0.5);
            result = amrex::max(result, std::hypot(dx, dy));
        }
    }
    return result;
}

FrontResult
run_limited_front ()
{
    Mesh mesh = make_front_mesh();
    Vector<Real> extinction(mesh.cells.size(), Real(0.01));
    Real constexpr ambient_energy = Real(1.e-4);
    Vector<Real> state(mesh.cells.size(), ambient_energy);
    Real constexpr final_time = Real(0.15);
    int constexpr steps = 48;
    Real constexpr dt = final_time / Real(steps);
    Real constexpr relaxation = Real(0.7);
    Real const picard_tolerance =
        (sizeof(Real) == sizeof(float)) ? Real(2.e-4) : Real(2.e-5);
    int constexpr maximum_picard_iterations = 75;

    FrontResult result;
    result.cells = static_cast<Long>(mesh.cells.size());
    for (int step = 0; step < steps; ++step) {
        Vector<Real> const old_state = state;
        bool converged = false;
        int step_iterations = 0;
        for (int iteration = 0; iteration < maximum_picard_iterations;
             ++iteration) {
            auto diffusion = compute_diffusion(mesh, state, extinction, true);
            auto system = assemble_system(mesh, diffusion, old_state, dt, true);
            AMGGMRESSolver solver(system.matrix);
            record_setup(result.solver, solver);
            auto solution = solver.solve(system.rhs);
            record_solve(result.solver, solution);

            result.final_picard_change =
                maximum_relative_change(solution.values, state);
            ++step_iterations;
            ++result.total_picard_iterations;
            if (result.final_picard_change <= picard_tolerance) {
                state = std::move(solution.values);
                converged = true;
                break;
            }
            for (std::size_t row = 0; row < state.size(); ++row) {
                state[row] = relaxation * solution.values[row] +
                             (Real(1) - relaxation) * state[row];
            }
        }
        result.maximum_picard_iterations =
            amrex::max(result.maximum_picard_iterations, step_iterations);
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            converged,
            "The limited-front FLD Picard iteration did not converge");
    }

    compute_diffusion(mesh, state, extinction, true,
                      &result.maximum_flux_fraction);
    result.causal_radius = Real(0.1) + final_time;
    result.front_radius = front_radius(mesh, state);
    result.far_excess =
        far_excess(mesh, state, result.causal_radius + Real(2) * mesh.fine_h,
                   ambient_energy);
    auto const [minimum, maximum] = minimum_maximum(state);
    result.minimum_energy = minimum;
    result.maximum_energy = maximum;

    Vector<Real> unlimited_state(mesh.cells.size(), ambient_energy);
    auto unlimited_diffusion =
        compute_diffusion(mesh, unlimited_state, extinction, false);
    auto unlimited_system =
        assemble_system(mesh, unlimited_diffusion, unlimited_state, dt, true);
    AMGGMRESSolver unlimited_solver(unlimited_system.matrix);
    Vector<Real> unlimited_boundary_rhs(mesh.cells.size());
    for (std::size_t row = 0; row < unlimited_state.size(); ++row) {
        unlimited_boundary_rhs[row] =
            unlimited_system.rhs[row] - unlimited_state[row];
    }
    for (int step = 0; step < steps; ++step) {
        Vector<Real> rhs = unlimited_state;
        for (std::size_t row = 0; row < rhs.size(); ++row) {
            rhs[row] += unlimited_boundary_rhs[row];
        }
        auto solution = unlimited_solver.solve(rhs);
        unlimited_state = std::move(solution.values);
    }
    result.unlimited_far_excess = far_excess(
        mesh, unlimited_state, result.causal_radius + Real(2) * mesh.fine_h,
        ambient_energy);

    Real const causality_tolerance =
        (sizeof(Real) == sizeof(float)) ? Real(2.e-4) : Real(2.e-8);
    AMREX_ALWAYS_ASSERT(result.maximum_flux_fraction <=
                        Real(1) + causality_tolerance);
    AMREX_ALWAYS_ASSERT(result.maximum_flux_fraction > Real(0.95));
    AMREX_ALWAYS_ASSERT(result.front_radius <=
                        result.causal_radius + Real(2) * mesh.fine_h);
    AMREX_ALWAYS_ASSERT(result.front_radius >=
                        result.causal_radius - Real(4) * mesh.fine_h);
    AMREX_ALWAYS_ASSERT(result.minimum_energy >= ambient_energy - Real(2.e-5));
    AMREX_ALWAYS_ASSERT(result.maximum_energy <= Real(1) + Real(2.e-5));
    AMREX_ALWAYS_ASSERT(result.far_excess < Real(0.015));
    AMREX_ALWAYS_ASSERT(result.unlimited_far_excess > Real(0.04));
    return result;
}

void
print_solver_summary (SolverSummary const& solver)
{
    amrex::Print() << "solves=" << solver.solves
                   << ", GMRES avg/max=" << solver.average_iterations() << "/"
                   << solver.maximum_iterations
                   << ", max true relative residual="
                   << solver.maximum_relative_residual
                   << ", max AMG levels=" << solver.maximum_levels
                   << ", max operator complexity="
                   << solver.maximum_operator_complexity
                   << ", aggregate setup=" << solver.setup_seconds << " s";
}

} // namespace

int
main (int argc, char* argv[])
{
    amrex::Initialize(argc, argv);
    {
        static_assert(AMREX_SPACEDIM == 2);

        auto const gaussian_uniform = run_gaussian(false);
        amrex::Print() << "FLD Gaussian uniform: cells="
                       << gaussian_uniform.cells << ", relative L1 error="
                       << gaussian_uniform.relative_l1_error
                       << ", relative energy drift="
                       << gaussian_uniform.relative_energy_drift << ", ";
        print_solver_summary(gaussian_uniform.solver);
        amrex::Print() << '\n';

        auto const gaussian_amr = run_gaussian(true);
        amrex::Print() << "FLD Gaussian AMR: cells=" << gaussian_amr.cells
                       << ", relative L1 error="
                       << gaussian_amr.relative_l1_error
                       << ", relative energy drift="
                       << gaussian_amr.relative_energy_drift << ", ";
        print_solver_summary(gaussian_amr.solver);
        amrex::Print() << '\n';
        AMREX_ALWAYS_ASSERT(gaussian_amr.relative_l1_error <=
                            Real(2) * gaussian_uniform.relative_l1_error);

        auto const cloud_uniform = run_cloud(false);
        amrex::Print() << "FLD cloud uniform: cells=" << cloud_uniform.cells
                       << ", transmission=" << cloud_uniform.transmission
                       << ", balance error=" << cloud_uniform.balance_error
                       << ", mixed cells/cloud area error="
                       << cloud_uniform.mixed_cells << "/"
                       << cloud_uniform.cloudy_area_relative_error
                       << ", E range=[" << cloud_uniform.minimum_energy << ","
                       << cloud_uniform.maximum_energy << "]"
                       << ", Picard iterations/change="
                       << cloud_uniform.picard_iterations << "/"
                       << cloud_uniform.final_picard_change << ", ";
        print_solver_summary(cloud_uniform.solver);
        amrex::Print() << '\n';

        auto const cloud_amr = run_cloud(true);
        amrex::Print() << "FLD cloud AMR: cells=" << cloud_amr.cells
                       << ", transmission=" << cloud_amr.transmission
                       << ", balance error=" << cloud_amr.balance_error
                       << ", mixed cells/cloud area error="
                       << cloud_amr.mixed_cells << "/"
                       << cloud_amr.cloudy_area_relative_error
                       << ", E range=[" << cloud_amr.minimum_energy << ","
                       << cloud_amr.maximum_energy << "]"
                       << ", Picard iterations/change="
                       << cloud_amr.picard_iterations << "/"
                       << cloud_amr.final_picard_change << ", ";
        print_solver_summary(cloud_amr.solver);
        amrex::Print() << '\n';

        Real const transmission_difference =
            std::abs(cloud_amr.transmission - cloud_uniform.transmission) /
            cloud_uniform.transmission;
        amrex::Print() << "FLD cloud AMR/fine transmission difference="
                       << transmission_difference << '\n';
        AMREX_ALWAYS_ASSERT(transmission_difference < Real(0.12));

        auto const front = run_limited_front();
        amrex::Print() << "FLD limited front: cells=" << front.cells
                       << ", front/causal radius=" << front.front_radius << "/"
                       << front.causal_radius
                       << ", far excess=" << front.far_excess
                       << ", unlimited far excess="
                       << front.unlimited_far_excess
                       << ", max |F|/(cE)=" << front.maximum_flux_fraction
                       << ", E range=[" << front.minimum_energy << ","
                       << front.maximum_energy << "]"
                       << ", Picard total/max/change="
                       << front.total_picard_iterations << "/"
                       << front.maximum_picard_iterations << "/"
                       << front.final_picard_change << ", ";
        print_solver_summary(front.solver);
        amrex::Print() << '\n';

        amrex::Print() << "2-D scattering-only FLD Gaussian, cloud-layer, "
                       << "and limited-front GMRES+AMG tests passed\n";
    }
    amrex::Finalize();
}

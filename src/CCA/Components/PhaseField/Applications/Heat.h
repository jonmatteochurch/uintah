/*
 * The MIT License
 *
 * Copyright (c) 1997-2020 The University of Utah
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * @file CCA/Components/PhaseField/Applications/Heat.h
 * @author Jon Matteo Church [j.m.church@leeds.ac.uk]
 * @date 2018/12
 */

#ifndef Packages_Uintah_CCA_Components_PhaseField_Applications_Heat_h
#define Packages_Uintah_CCA_Components_PhaseField_Applications_Heat_h

#include <sci_defs/hypre_defs.h>

#ifdef HAVE_HYPRE
#   include <CCA/Components/Solvers/HypreSStruct/AdditionalEntriesP.h>
#   include <CCA/Components/Solvers/HypreSStruct/Solver.h>
#endif

#include <CCA/Components/PhaseField/Util/Definitions.h>
#include <CCA/Components/PhaseField/Util/Expressions.h>
#include <CCA/Components/PhaseField/DataTypes/HeatProblem.h>
#include <CCA/Components/PhaseField/DataTypes/SubProblems.h>
#include <CCA/Components/PhaseField/DataTypes/ScalarField.h>
#include <CCA/Components/PhaseField/DataTypes/VectorField.h>
#include <CCA/Components/PhaseField/DataTypes/ReferenceGrid.h>
#include <CCA/Components/PhaseField/Applications/Application.h>
#include <CCA/Components/PhaseField/Views/View.h>
#include <CCA/Components/PhaseField/Views/FDView.h>
#include <CCA/Components/PhaseField/DataWarehouse/DWView.h>
#include <CCA/Components/PhaseField/AMR/AMRInterpolator.h>
#include <CCA/Components/PhaseField/AMR/AMRRestrictor.h>

#include <CCA/Ports/Regridder.h>

#include <Core/Util/Factory/Implementation.h>
#include <Core/Util/DebugStream.h>
#include <Core/Grid/SimpleMaterial.h>
#include <Core/Grid/Variables/PerPatchVars.h>

/**
 * @brief Enable matrix entries variables for debugging
 *
 * When not null one scalar variable for each stencil entry is made available
 * to be saved and debugged
 * - Ap: diagonal entry
 * - Aw: west (x-) entry
 * - Ae: east (x+)  entry
 * - As: south (y-) entry
 * - An: north (y+) entry
 * - Ab: bottom (z-) entry
 * - At: top (z+) entry
 * .
 */
#undef PhaseField_Heat_DBG_MATRIX
// #define PhaseField_Heat_DBG_MATRIX

#undef EXACT_NORM
// #define EXACT_NORM

namespace Uintah
{
namespace PhaseField
{

/// Debugging stream for component schedulings
static constexpr bool dbg_heat_scheduling = false;

/**
 * @brief Heat PhaseField applications
 *
 * Implements a Finite Difference solver for the simulation of the heat
 * diffusion model
 * \f[
 * \dot u = \alpha \nabla^2 u
 * \f]
 * with initial data
 * \f[
 * u_{|t=0} = \prod_{d} \cos ( \alpha x_d );
 * \f]
 * where \f$d\f$ ranges over the problem dimension.
 *
 * The model parameters are:
 * - \f$ \alpha \f$   thermal diffusivity
 *
 * @todo templetize FAC/nonFAC
 * @todo check for more than 2 amr levels
 *
 * @tparam VAR type of variable representation
 * @tparam DIM problem dimensions
 * @tparam STN finite-difference stencil
 * @tparam AMR whether to use adaptive mesh refinement
 */
template<VarType VAR, DimType DIM, StnType STN, bool AMR = false, bool TST = false>
class Heat
    : public Application< HeatProblem<VAR, STN, TST>, AMR >
    , public Implementation< Heat<VAR, DIM, STN, AMR, TST>, UintahParallelComponent, const ProcessorGroup *, const MaterialManagerP, int >
{

private: // STATIC MEMBERS

    /// Index for solution
    static constexpr size_t U = 0;

    /// Index for solution first order derivatives (for TST = true)
    static constexpr size_t DU = 1;

    /// Index for solution second order derivatives (for TST = true)
    static constexpr size_t DDU = 2;

    /// Problem material index (only one SimpleMaterial)
    static constexpr int material = 0;

    /// Number of ghost elements required by STN (on the same level)
    using Application< HeatProblem<VAR, STN, TST> >::FGN;

    /// Type of ghost elements required by VAR and STN (on coarser level)
    using Application< HeatProblem<VAR, STN, TST> >::FGT;

    /// Number of ghost elements required by STN (on coarser level)
    using Application< HeatProblem<VAR, STN, TST> >::CGN;

    /// Type of ghost elements required by VAR and STN (on the same level)
    using Application< HeatProblem<VAR, STN, TST> >::CGT;

    /// Interpolation type for refinement
    using Application< HeatProblem<VAR, STN, TST> >::C2F;

    /// Restriction type for coarsening
    using Application< HeatProblem<VAR, STN, TST> >::F2C;

    /// Number of ghost elements required by intepolation scheme
    using Application< HeatProblem<VAR, STN, TST> >::IGN;

    /// Type of ghost elements required by intepolation scheme
    using Application< HeatProblem<VAR, STN, TST> >::IGT;

#ifdef HAVE_HYPRE
    /// Non-stencil entries type for implicit matrix
    using _AdditionalEntries = PerPatch<HypreSStruct::AdditionalEntriesP>;
#endif

public: // STATIC MEMBERS

    /// Class name as used by ApplicationFactory
    const static FactoryString Name;

protected: // MEMBERS

    /// Label for the solution in the DataWarehouse
    const VarLabel * u_label;

    /// Label for the difference between computed and analytical solution
    const VarLabel * epsilon_u_label;

    /// Label for the local error in u in the DataWarehouse
    const VarLabel * error_u_label;

    /// Label for the square of the discrete H0-norm of the solution in the DataWarehouse
    const VarLabel * u_norm2_L2_label;

    /// Label for the square of the global H0-error of the solution in the DataWarehouse
    const VarLabel * error_norm2_L2_label;

#ifdef PhaseField_Heat_DBG_DERIVATIVES
    /// Label for the gradient vector of the solution in the DataWarehouse
    std::array<const VarLabel *, DIM> du_label;

    /// Label for the difference between computed and analytical solution first order derivatives
    std::array<const VarLabel *, DIM> epsilon_du_label;

    /// Label for the local error in the first order derivatives of u in the DataWarehouse
    std::array<const VarLabel *, DIM> error_du_label;

    /// Label for the square of the discrete H1-seminorm of the solution in the DataWarehouse
    const VarLabel * u_norm2_H10_label;

    /// Label for the square of the global H1-error of the solution in the DataWarehouse
    const VarLabel * error_norm2_H10_label;

    /// Label for the second order derivatives vector of the solution in the DataWarehouse
    std::array<const VarLabel *, DIM> ddu_label;

    /// Label for the difference between computed and analytical solution second order derivatives
    std::array<const VarLabel *, DIM> epsilon_ddu_label;

    /// Label for the local error in the second order derivatives of u in the DataWarehouse
    std::array<const VarLabel *, DIM> error_ddu_label;

    /// Label for the square of the discrete H2-seminorm of the solution in the DataWarehouse
    const VarLabel * u_norm2_H20_label;

    /// Label for the square of the global H2-error of the solution in the DataWarehouse
    const VarLabel * error_norm2_H20_label;
#endif // PhaseField_Heat_DBG_DERIVATIVES

#ifdef HAVE_HYPRE
    /// Label for the implicit matrix stencil entries in the DataWarehouse
    const VarLabel * matrix_label;

    /// Label for the implicit vector in the DataWarehouse
    const VarLabel * rhs_label;

    /// Label for the implicit matrix non-stecil entries in the DataWarehouse
    const VarLabel * additional_entries_label;

#   ifdef PhaseField_Heat_DBG_MATRIX
    /// Label for the diagonal entry of the matrix stencil in the DataWarehouse
    const VarLabel * Ap_label;

    /// Label for the west (x-) entry of the matrix stencil in the DataWarehouse
    const VarLabel * Aw_label;

    /// Label for the east (x+) entry of the matrix stencil in the DataWarehouse
    const VarLabel * Ae_label;

    /// Label for the south (y-) entry of the matrix stencil in the DataWarehouse
    const VarLabel * As_label;

    /// Label for the north (y+) entry of the matrix stencil in the DataWarehouse
    const VarLabel * An_label;

    /// Label for the bottom (z-) entry of the matrix stencil in the DataWarehouse
    const VarLabel * Ab_label;

    /// Label for the top (x+) entry of the matrix stencil in the DataWarehouse
    const VarLabel * At_label;

#   endif // PhaseField_Heat_DBG_MATRIX
#endif // HAVE_HYPRE

    /// Time step size
    double delt;

    /// Non-dimensional thermal diffusivity
    double alpha;

    /// Threshold for AMR
    double refine_threshold;

#ifdef HAVE_HYPRE
    /// Time advance scheme
    TS time_scheme;
#endif

    bool is_time_advance_solution_scheduled;
    bool is_time_advance_solution_error_scheduled;
    bool is_time_advance_dbg_derivatives_scheduled;
    bool is_time_advance_dbg_derivatives_error_scheduled;

public: // CONSTRUCTORS/DESTRUCTOR

    /**
     * @brief Constructor
     *
     * Intantiate an Heat application
     *
     * @param myWorld data structure to manage mpi processes
     * @param materialManager data structure to manage materials
     * @param verbosity constrols amount of debugging output
     */
    Heat (
        const ProcessorGroup * myWorld,
        const MaterialManagerP materialManager,
        int verbosity = 0
    );

    /**
     * @brief Destructor
     */
    virtual ~Heat();

    /// Prevent copy (and move) constructor
    Heat ( Heat const & ) = delete;

    /// Prevent copy (and move) assignment
    /// @return deleted
    Heat & operator= ( Heat const & ) = delete;

protected: // SETUP

    /**
     * @brief Setup
     *
     * Initialize problem parameters with values from problem specifications
     *
     * @param params problem specifications parsed from input file
     * @param restart_prob_spec unused
     * @param grid unused
     */
    virtual void
    problemSetup (
        const ProblemSpecP & params,
        const ProblemSpecP & restart_prob_spec,
        GridP & grid
    ) override;

    /**
     * @brief Setup Boundary Variables
     * (default implementation)
     *
     * Specify the label for u as the one to be considered when partitioning
     * in subproblems
     */
    template < bool T >
    typename std::enable_if < !T, void >::type
    problemSetup_boundary_variables (
    )
    {
        this->setBoundaryVariables ( u_label );
    }

    /**
     * @brief Setup Boundary Variables
     * (test implementation)
     *
     * Specify the label for u and its derivatives as those to be considered
     * when partitioning in subproblems
     */
    template < bool T >
    typename std::enable_if < T, void >::type
    problemSetup_boundary_variables (
    )
    {
#ifdef PhaseField_Heat_DBG_DERIVATIVES
        this->setBoundaryVariables ( u_label, du_label, ddu_label );
#else
        this->setBoundaryVariables ( u_label );
#endif
    }

protected: // SCHEDULINGS

    /**
     * @brief Schedule the initialization tasks
     *
     * Specify all tasks to be performed at initial timestep to initialize
     * variables in the DataWarehouse
     *
     * @param level grid level to be initialized
     * @param sched scheduler to manage the tasks
     */
    virtual void
    scheduleInitialize (
        const LevelP & level,
        SchedulerP & sched
    ) override;

    /**
     * @brief Schedule task_initialize_solution (non AMR implementation)
     *
     * Defines the dependencies and output of the task which initializes the
     * solution allowing sched to control its execution order
     *
     * @param level grid level to be initialized
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    scheduleInitialize_solution (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_initialize_solution (AMR implementation)
     *
     * Defines the dependencies and output of the task which initializes the
     * solution allowing sched to control its execution order
     *
     * @param level grid level to be initialized
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    scheduleInitialize_solution (
        const LevelP & level,
        SchedulerP & sched
    );

#ifdef PhaseField_Heat_DBG_DERIVATIVES
    /**
     * @brief Schedule task_time_advance_dbg_derivatives (coarsest level implementation)
     *
     * Defines the dependencies and output of the task which updates the
     * solution derivatives allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    scheduleInitialize_dbg_derivatives (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_dbg_derivatives (refinement level implementation)
     *
     * Defines the dependencies and output of the task which updates the
     * solution derivatives allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    scheduleInitialize_dbg_derivatives (
        const LevelP & level,
        SchedulerP & sched
    );
#endif // PhaseField_Heat_DBG_DERIVATIVES

    /**
     * @brief Schedule the initialization tasks for restarting a simulation
     *
     * Specify all tasks to be performed at fist timestep after a stop to
     * initialize not saved variables to the DataWarehouse
     *
     * @param level grid level to be initialized
     * @param sched scheduler to manage the tasks
     */
    virtual void
    scheduleRestartInitialize (
        const LevelP & level,
        SchedulerP & sched
    ) override;

    /**
     * @brief Schedule task_compute_stable_timestep
     *
     * Specify all tasks to be performed before each time advance to compute a
     * timestep size which ensures numerical stability
     *
     * @param level grid level to be initialized
     * @param sched scheduler to manage the tasks
     */
    virtual void
    scheduleComputeStableTimeStep (
        const LevelP & level,
        SchedulerP & sched
    ) override;

    /**
     * @brief Schedule the time advance tasks
     *
     * Specify all tasks to be performed at each timestep to update the
     * simulation variables in the DataWarehouse
     *
     * @param level grid level to be initialized
     * @param sched scheduler to manage the tasks
     */
    virtual void
    scheduleTimeAdvance (
        const LevelP & level,
        SchedulerP & sched
    ) override;

#ifdef PhaseField_Heat_DBG_DERIVATIVES
    /**
     * @brief Schedule task_time_advance_dbg_derivatives (coarsest level implementation)
     *
     * Defines the dependencies and output of the task which updates the
     * solution derivatives allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    scheduleTimeAdvance_dbg_derivatives (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_dbg_derivatives (refinement level implementation)
     *
     * Defines the dependencies and output of the task which updates the
     * solution derivatives allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    scheduleTimeAdvance_dbg_derivatives (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule update of solution derivatives errors
     * (default implementation)
     *
     * Do nothing
     */
    template < bool MG, bool T >
    typename std::enable_if < !T, void >::type
    scheduleTimeAdvance_dbg_derivatives_error (
        const LevelP &,
        SchedulerP &
    ) {};

    /**
     * @brief Schedule task_time_advance_dbg_derivatives_error
     * (test, non AMR implementation)
     *
     * Defines the dependencies and output of the task which updates the
     * error on solution derivatives allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG, bool T >
    typename std::enable_if < T && !MG, void >::type
    scheduleTimeAdvance_dbg_derivatives_error (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_dbg_derivatives_error
     * (test, AMR implementation)
     *
     * Defines the dependencies and output of the task which updates the
     * error on solution derivatives allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG, bool T >
    typename std::enable_if < T && MG, void >::type
    scheduleTimeAdvance_dbg_derivatives_error (
        const LevelP & level,
        SchedulerP & sched
    );
#endif

    /**
     * @brief Schedule task_time_advance_solution
     *
     * Switches between available implementations depending on the given time
     * scheme
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    void
    scheduleTimeAdvance_solution (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_forward_euler
     * (coarsest level implementation)
     *
     * Defines the dependencies and output of the task which updates u
     * allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    scheduleTimeAdvance_solution_forward_euler (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_forward_euler
     * (refinement level implementation)
     *
     * Defines the dependencies and output of the task which updates u
     * allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    scheduleTimeAdvance_solution_forward_euler (
        const LevelP & level,
        SchedulerP & sched
    );

#ifdef HAVE_HYPRE
    /**
     * @brief Schedule task_time_advance_solution_implicit
     * (non AMR implementation)
     *
     * Switches between available implementations depending on the given time
     * solver
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    scheduleTimeAdvance_solution_implicit (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_implicit
     * (AMR implementation)
     *
     * Switches between available implementations depending on the given time
     * solver
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    scheduleTimeAdvance_solution_implicit (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_backward_euler_assemble_hypre
     * (coarsest level implementation)
     *
     * Defines the dependencies and output of the task which assembles the
     * implicit system allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    scheduleTimeAdvance_solution_backward_euler_assemble_hypre (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_backward_euler_assemble_hypre
     * (refinement level implementation)
     *
     * Defines the dependencies and output of the task which assembles the
     * implicit system allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    scheduleTimeAdvance_solution_backward_euler_assemble_hypre (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_backward_euler_assemble_hypresstruct
     * (coarsest level implementation)
     *
     * Defines the dependencies and output of the task which assembles the
     * implicit system allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    scheduleTimeAdvance_solution_backward_euler_assemble_hypresstruct (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_backward_euler_assemble_hypresstruct
     * (refinement level implementation)
     *
     * Defines the dependencies and output of the task which assembles the
     * implicit system allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    scheduleTimeAdvance_solution_backward_euler_assemble_hypresstruct (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_crank_nicolson_assemble_hypre
     * (coarsest level implementation)
     *
     * Defines the dependencies and output of the task which assembles the
     * implicit system allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    scheduleTimeAdvance_solution_crank_nicolson_assemble_hypre (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_crank_nicolson_assemble_hypre
     * (refinement level implementation)
     *
     * Defines the dependencies and output of the task which assembles the
     * implicit system allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    scheduleTimeAdvance_solution_crank_nicolson_assemble_hypre (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_crank_nicolson_assemble_hypresstruct
     * (coarsest level implementation)
     *
     * Defines the dependencies and output of the task which assembles the
     * implicit system allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    scheduleTimeAdvance_solution_crank_nicolson_assemble_hypresstruct (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_crank_nicolson_assemble_hypresstruct
     * (refinement level implementation)
     *
     * Defines the dependencies and output of the task which assembles the
     * implicit system allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    scheduleTimeAdvance_solution_crank_nicolson_assemble_hypresstruct (
        const LevelP & level,
        SchedulerP & sched
    );

    void
    scheduleTimeAdvannce_comunicate_before_solve_hypresstruct (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_update_dbg_matrix
     *
     * Defines the dependencies and output of the task which updates the fields
     * used for debugging the entries of the implicit matrix stencil
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
#   ifdef PhaseField_Heat_DBG_MATRIX
    void
    scheduleTimeAdvance_update_dbg_matrix (
        const LevelP & level,
        SchedulerP & sched
    );
#   endif // PhaseField_Heat_DBG_MATRIX
#endif // HAVE_HYPRE

    /**
     * @brief Schedule update of solution errors
     * (default implementation)
     *
     * Do nothing
     */
    template < bool MG, bool T >
    typename std::enable_if < !T, void >::type
    scheduleTimeAdvance_solution_error (
        const LevelP &,
        SchedulerP &
    ) {};

    /**
     * @brief Schedule task_time_advance_solution_error
     * (test, non AMR implementation)
     *
     * Defines the dependencies and output of the task which updates the
     * error on solution allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG, bool T >
    typename std::enable_if < T && !MG, void >::type
    scheduleTimeAdvance_solution_error (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_time_advance_solution_error
     * (test, AMR implementation)
     *
     * Defines the dependencies and output of the task which updates the
     * error on solution allowing sched to control its execution order
     *
     * @param level grid level to be updated
     * @param sched scheduler to manage the tasks
     */
    template < bool MG, bool T >
    typename std::enable_if < T && MG, void >::type
    scheduleTimeAdvance_solution_error (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule the refinement tasks
     *
     * Specify all tasks to be performed after an AMR regrid in order to populate
     * variables in the DataWarehouse at newly created patches
     *
     * @remark If regridding happens at initial time step scheduleInitialize is
     * called instead
     *
     * @param new_patches patches to be populated
     * @param sched scheduler to manage the tasks
     */
    virtual void
    scheduleRefine (
        const PatchSet * new_patches,
        SchedulerP & sched
    ) override;

    /**
     * @brief Schedule task_refine_solution
     *
     * Defines the dependencies and output of the task which interpolates the
     * solution from the coarser level to each one of the new_patches
     * allowing sched to control its execution order
     *
     * @param new_patches patches to be populated
     * @param sched scheduler to manage the tasks
     */
    void
    scheduleRefine_solution (
        const PatchSet * new_patches,
        SchedulerP & sched
    );

    /**
     * @brief Schedule the refinement tasks
     *
     * Do nothing
     *
     * @param level_fine unused
     * @param sched unused
     * @param need_old_coarse unused
     * @param need_new_coarse unused
     */
    virtual void
    scheduleRefineInterface (
        const LevelP & level_fine,
        SchedulerP & sched,
        bool need_old_coarse,
        bool need_new_coarse
    ) override;

    /**
     * @brief Schedule the time coarsen tasks
     *
     * Specify all tasks to be performed after each timestep to restrict the
     * computed variables from finer to coarser levels
     *
     * @param level_coarse level to be updated
     * @param sched scheduler to manage the tasks
     */
    virtual void
    scheduleCoarsen (
        const LevelP & level_coarse,
        SchedulerP & sched
    ) override;

    /**
     * @brief Schedule task_coarsen_solution
     *
     * Defines the dependencies and output of the task which restrict the
     * solution to level_coarse from its finer level allowing sched to control
     * its execution order
     *
     * @param level_coarse level to be updated
     * @param sched scheduler to manage the tasks
     */
    void
    scheduleCoarsen_solution (
        const LevelP & level_coarse,
        SchedulerP & sched
    );

    /**
     * @brief Schedule the error estimate tasks
     *
     * Specify all tasks to be performed before each timestep to estimate the
     * spatial discretization error on the solution update in order to decide
     * where to refine the grid
     *
     * @param level level to check
     * @param sched scheduler to manage the tasks
     */
    virtual void
    scheduleErrorEstimate (
        const LevelP & level,
        SchedulerP & sched
    ) override;

    /**
     * @brief Schedule task_error_estimate_solution (coarsest level implementation)
     *
     * Defines the dependencies and output of the task which estimates the
     * spatial discretization error allowing sched to controvaluel its execution order
     *
     * @param level level to check
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    scheduleErrorEstimate_solution (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule task_error_estimate_solution (refinement level implementation)
     *
     * Defines the dependencies and output of the task which estimates the
     * spatial discretization error allowing sched to control its execution order
     *
     * @param level level to check
     * @param sched scheduler to manage the tasks
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    scheduleErrorEstimate_solution (
        const LevelP & level,
        SchedulerP & sched
    );

    /**
     * @brief Schedule the initial error estimate tasks
     *
     * Specify all tasks to be performed before the first timestep to estimate
     * the spatial discretization error on the solution update in order to decide
     * where to refine the grid
     *
     * @remark forward to scheduleErrorEstimate
     *
     * @param level level to check
     * @param sched scheduler to manage the tasks
     */
    virtual void
    scheduleInitialErrorEstimate (
        const LevelP & level,
        SchedulerP & sched
    ) override;

protected: // TASKS

    /**
     * @brief Initialize solution task
     *
     * Allocate and save variables for u for each one of the patches
     * and save them to dw_new
     * @remark initialize also anisotropy terms to 0
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old unused
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_initialize_solution (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Initialize hypre sstruct structures on restart task
     *
     * Allocate and save variables for non-stencil variables for each
     * one of the patches and save them to dw_new
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old unused
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_restart_initialize_hypresstruct (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Compute timestep task
     *
     * Puts into the new DataWarehouse the constant value specified in input (delt)
     * of the timestep
     *
     * When test is set to true it also checks that the solution norm is stable
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_compute_stable_timestep (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

#ifdef PhaseField_Heat_DBG_DERIVATIVES
    /**
     * @brief Advance derivatives task (debug)
     *
     * Computes value of u derivatives using the solution at the previous
     * timestep (for debugging purpose)
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_dbg_derivatives (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Advance derivatives error task
     * (test, non AMR implementation)
     *
     * Computes the error in the approximation of u derivatives
     * comparing them with their analytical expressions (for debugging purpose)
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    template < bool MG, bool T >
    typename std::enable_if < T && !MG, void >::type
    task_time_advance_dbg_derivatives_error (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Advance derivatives error task
     * (test, AMR implementation)
     *
     * Computes the error in the approximation of u derivatives
     * comparing them with their analytical expressions (for debugging purpose)
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    template < bool MG, bool T >
    typename std::enable_if < T && MG, void >::type
    task_time_advance_dbg_derivatives_error (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );
#endif // PhaseField_Heat_DBG_DERIVATIVES

    /**
     * @brief Advance solution task (Forward Euler implementation)
     *
     * Computes new value of u using the value of the solution and at
     * previous timestep
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_forward_euler (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

#ifdef HAVE_HYPRE
    /**
     * @brief Assemble hypre system task (Backward Euler implementation)
     *
     * Switches between available implementations to avoid assembling the matrix
     * when not required
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_backward_euler_assemble_hypre (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre system task (Backward Euler, full implementation)
     *
     * Assemble both implicit matrix and vector for hypre using the value of the
     * solution and at previous timestep
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_backward_euler_assemble_hypre_full (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre system task (Backward Euler, rhs implementation)
     *
     * Assemble only implicit vector for hypre using the value of the
     * solution and at previous timestep
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_backward_euler_assemble_hypre_rhs (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre sstruct system task (Backward Euler implementation)
     *
     * Switches between available implementations to avoid assembling the matrix
     * when not required
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_backward_euler_assemble_hypresstruct (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre sstruct system task (Backward Euler, full implementation)
     *
     * Assemble both implicit matrix and vector for hypre using the value of the
     * solution and at previous timestep
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_backward_euler_assemble_hypresstruct_full (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre sstruct system task (Backward Euler, rhs implementation)
     *
     * Assemble only implicit vector for hypre using the value of the
     * solution and at previous timestep
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_backward_euler_assemble_hypresstruct_rhs (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre system task (Crank Nicolson implementation)
     *
     * Switches between available implementations to avoid assembling the matrix
     * when not required
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_crank_nicolson_assemble_hypre (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre system task (Crank Nicolson, full implementation)
     *
     * Assemble both implicit matrix and vector for hypre using the value of the
     * solution and at previous timestep
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_crank_nicolson_assemble_hypre_full (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre system task (Crank Nicolson, rhs implementation)
     *
     * Assemble only implicit vector for hypre using the value of the
     * solution and at previous timestep
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_crank_nicolson_assemble_hypre_rhs (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre sstruct system task (Crank Nicolson implementation)
     *
     * Switches between available implementations to avoid assembling the matrix
     * when not required
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_crank_nicolson_assemble_hypresstruct (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre sstruct system task (Crank Nicolson, full implementation)
     *
     * Assemble both implicit matrix and vector for hypre using the value of the
     * solution and at previous timestep
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_crank_nicolson_assemble_hypresstruct_full (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Assemble hypre sstruct system task (Crank Nicolson, rhs implementation)
     *
     * Assemble only implicit vector for hypre using the value of the
     * solution and at previous timestep
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_time_advance_solution_crank_nicolson_assemble_hypresstruct_rhs (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    void
    task_communicate_before_solve (
        const ProcessorGroup * /*myworld*/,
        const PatchSubset * /*patches*/,
        const MaterialSubset * /*matls*/,
        DataWarehouse * /*dw_old*/,
        DataWarehouse * /*dw_new*/
    ) {}

    /**
     * @brief Update stencil entries debugging views task
     *
     * Copy new value of the debugging views from the implicit matrix
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
#   ifdef PhaseField_Heat_DBG_MATRIX
    void
    task_time_advance_update_dbg_matrix (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );
#   endif // PhaseField_Heat_DBG_MATRIX
#endif // HAVE_HYPRE

    /**
     * @brief Advance solution error task (test, coarsest level implementation)
     *
     * Computes error in u approximation using the analytical solution
     *
     * @remark test must be set to true in input
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    template < bool MG, bool T >
    typename std::enable_if < T && !MG, void >::type
    task_time_advance_solution_error (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Advance solution error task (test, refinement level implementation)
     *
     * Computes error in u approximation using the analytical solution
     *
     * @remark test must be set to true in input
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    template < bool MG, bool T >
    typename std::enable_if < T && MG, void >::type
    task_time_advance_solution_error (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Refine solution task
     *
     * Computes interpolated value of u on new refined patched
     *
     * @param myworld data structure to manage mpi processes
     * @param patches_fine list of patches to be initialized
     * @param matls unused
     * @param dw_old unused
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_refine_solution (
        const ProcessorGroup * myworld,
        const PatchSubset * patches_fine,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief Coarsen solution task
     *
     * Restricted value of u from refined regions to coarse patches
     * underneath
     *
     * @param myworld data structure to manage mpi processes
     * @param patches_coarse list of patches to be updated
     * @param matls unused
     * @param dw_old unused
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_coarsen_solution (
        const ProcessorGroup * myworld,
        const PatchSubset * patches_coarse,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

    /**
     * @brief ErrorEstimate solution task
     *
     * Computes the gradient of the solution using its value at the previous
     * timestep and set refinement flag where it is above the threshold given
     * in input
     *
     * @param myworld data structure to manage mpi processes
     * @param patches list of patches to be initialized
     * @param matls unused
     * @param dw_old DataWarehouse for previous timestep
     * @param dw_new DataWarehouse to be initialized
     */
    void
    task_error_estimate_solution (
        const ProcessorGroup * myworld,
        const PatchSubset * patches,
        const MaterialSubset * matls,
        DataWarehouse * dw_old,
        DataWarehouse * dw_new
    );

protected: // IMPLEMENTATIONS

    /**
     * @brief Initialize solution implementation
     *
     * compute initial condition for u at a given grid position
     *
     * @param id grid index
     * @param patch grid patch
     * @param L domain width
     * @param[out] u view of the solution field in the new dw
     */
    void
    initialize_solution (
        const IntVector & id,
        const Patch * patch,
        const double & L,
        View < ScalarField<double> > & u
    );

#ifdef PhaseField_Heat_DBG_DERIVATIVES

    /**
     * @brief Advance derivatives implementation
     *
     * Computes value of u derivatives using the solution at the previous
     * timestep at a given grid position (for debugging purpose)
     *
     * @param id grid index
     * @param u_old view of the solution field in the old dw
     * @param[out] du view of the solution derivatives vector field in the new dw
     * @param[out] ddu view of the solution derivatives vector field in the new dw
     */
    void
    time_advance_dbg_derivatives (
        const IntVector & id,
        const FDView < ScalarField<const double>, STN > & u_old,
        View < VectorField<double, DIM> > & du,
        View < VectorField<double, DIM> > & ddu
    );

    /**
     * @brief Advance derivatives epsilon implementation
     * (coarsest level implementation)
     *
     * Computes error in the approximation of u derivatives comparing them with
     * their analytical expressions at a given grid position
     * (for debugging purpose)
     *
     * @param id grid index
     * @param patch grid patch
     * @param ut \f$ \exp ( -\alpha d a^2 t ) \f$;
     * @param a \f$ \pi/2 L$ \f$
     * @param du view of the solution derivatives vector field in the new dw
     * @param ddu view of the solution derivatives vector field in the new dw
     * @param[out] epsilon_du view of the solution derivatives residual vector field in the new dw
     * @param[out] epsilon_ddu view of the solution derivatives residual vector field in the new dw
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    time_advance_dbg_derivatives_epsilon (
        const IntVector & id,
        const Patch * patch,
        const double & ut,
        const double & a,
        const View < VectorField<const double, DIM> > & du,
        const View < VectorField<const double, DIM> > & ddu,
        View < VectorField<double, DIM> > & epsilon_du,
        View < VectorField<double, DIM> > & epsilon_ddu
    );

    /**
     * @brief Advance derivatives epsilon implementation
     * (refined level implementation)
     *
     * Computes error in the approximation of u derivatives comparing them with
     * their analytical expressions at a given grid position
     * (for debugging purpose)
     *
     * @param id grid index
     * @param patch grid patch
     * @param ut \f$ \exp ( -\alpha d a^2 t ) \f$;
     * @param a \f$ \pi/2 L$ \f$
     * @param du view of the solution derivatives vector field in the new dw
     * @param ddu view of the solution derivatives vector field in the new dw
     * @param[out] epsilon_du view of the solution derivatives residual vector field in the new dw
     * @param[out] epsilon_ddu view of the solution derivatives residual vector field in the new dw
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    time_advance_dbg_derivatives_epsilon (
        const IntVector & id,
        const Patch * patch,
        const double & ut,
        const double & a,
        const View < VectorField<const double, DIM> > & du,
        const View < VectorField<const double, DIM> > & ddu,
        View < VectorField<double, DIM> > & epsilon_du,
        View < VectorField<double, DIM> > & epsilon_ddu
    );

    /**
     * @brief Advance derivatives error implementation
     * (coarsest level implementation)
     *
     * Computes error in the approximation of u derivatives comparing them with
     * their analytical expressions at a given grid position
     * (for debugging purpose)
     *
     * @param id grid index
     * @param patch grid patch
     * @param ut \f$ \exp ( -\alpha d a^2 t ) \f$;
     * @param a \f$ \pi/2 L$ \f$
     * @param area domain area
     * @param factor weight factor for discrete integrals
     * @param du view of the solution derivatives vector field in the new dw
     * @param ddu view of the solution derivatives vector field in the new dw
     * @param[out] error_du view of the solution derivatives error vector field in the new dw
     * @param[out] error_ddu view of the solution derivatives error vector field in the new dw
     * @param[out] u_norm2_H10 square of the L2-norm of the solution 1st order derivatives vector
     * @param[out] u_norm2_H20 square of the L2-norm of the solution 2nd order derivatives vector
     * @param[out] error_norm2_H10 L2-norm of the solution 1st order derivatives error vector
     * @param[out] error_norm2_H20 L2-norm of the solution 2nd order derivatives error vector
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    time_advance_dbg_derivatives_error (
        const IntVector & id,
        const Patch * patch,
        const double & ut,
        const double & a,
        const double & area,
        const double & factor,
        const View < VectorField<const double, DIM> > & du,
        const View < VectorField<const double, DIM> > & ddu,
        View < VectorField<double, DIM> > & error_du,
        View < VectorField<double, DIM> > & error_ddu,
        double & u_norm2_H10,
        double & u_norm2_H20,
        double & error_norm2_H10,
        double & error_norm2_H20
    );

    /**
     * @brief Advance derivatives error implementation
     * (refined level implementation)
     *
     * Computes error in the approximation of u derivatives comparing them with
     * their analytical expressions at a given grid position
     * (for debugging purpose)
     *
     * @param id grid index
     * @param patch grid patch
     * @param patch_finest reference grid patch
     * @param refinement grid refinement factors
     * @param ut \f$ \exp ( -\alpha d a^2 t ) \f$;
     * @param a \f$ \pi/2 L$ \f$
     * @param area domain area
     * @param factor weight factor for discrete integrals
     * @param du_finest view for evaluating the solution derivatives on the reference patch (interpolator)
     * @param ddu_finest view for evaluating the solution derivatives on the reference patch (interpolator)
     * @param[out] error_du view of the solution derivatives error vector field in the new dw
     * @param[out] error_ddu view of the solution derivatives error vector field in the new dw
     * @param[out] u_norm2_H10 square of the L2-norm of the solution 1st order derivatives vector
     * @param[out] u_norm2_H20 square of the L2-norm of the solution 2nd order derivatives vector
     * @param[out] error_norm2_H10 L2-norm of the solution 1st order derivatives error vector
     * @param[out] error_norm2_H20 L2-norm of the solution 2nd order derivatives error vector
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    time_advance_dbg_derivatives_error (
        const IntVector & id,
        const Patch * patch,
        const Patch * patch_finest,
        const IntVector & refinement,
        const double & ut,
        const double & a,
        const double & area,
        const double & factor,
        const View < VectorField<const double, DIM> > & du_finest,
        const View < VectorField<const double, DIM> > & ddu_finest,
        View < VectorField<double, DIM> > & error_du,
        View < VectorField<double, DIM> > & error_ddu,
        double & u_norm2_H10,
        double & u_norm2_H20,
        double & error_norm2_H10,
        double & error_norm2_H20
    );
#endif

    /**
     * @brief Advance solution implementation (Forward Euler implementation)
     *
     * compute new value for u at a given grid position using the value of the
     * solution and at previous timestep
     *
     * @param id grid index
     * @param u_old view of the solution field in the old dw
     * @param[out] u_new view of the solution field in the new dw
     */
    void
    time_advance_solution_forward_euler (
        const IntVector & id,
        const FDView < ScalarField<const double>, STN > & u_old,
        View < ScalarField<double> > & u_new
    );

#ifdef HAVE_HYPRE
    /**
     * @brief Assemble hypre system implementation (Backward Euler, full implementation)
     *
     * compute both implicit matrix stencil and vector entries at a given grid
     * position using the value of the solution and at previous timestep
     *
     * @param id grid index
     * @param u_old view of the solution field in the old dw
     * @param[out] A view of the implicit matrix stencil field in the new dw
     * @param[out] b view of the implicit vector field in the new dw
     */
    void
    time_advance_solution_backward_euler_assemble_hypre_full (
        const IntVector & id,
        const FDView < ScalarField<const double>, STN > & u_old,
        View < ScalarField<Stencil7> > & A,
        View < ScalarField<double> > & b
    );

    /**
     * @brief Assemble hypre system implementation (Backward Euler, rhs implementation)
     *
     * compute only implicit vector entries at a given grid
     * position using the value of the solution and at previous timestep
     *
     * @param id grid index
     * @param u_old view of the solution field in the old dw
     * @param[out] b view of the implicit vector field in the new dw
     */
    void
    time_advance_solution_backward_euler_assemble_hypre_rhs (
        const IntVector & id,
        const FDView < ScalarField<const double>, STN > & u_old,
        View < ScalarField<double> > & b
    );

    /**
     * @brief Assemble hypre sstruct system implementation (Backward Euler, full implementation)
     *
     * compute both implicit matrix stencil and vector entries at a given grid
     * position using the value of the solution and at previous timestep
     *
     * @param id grid index
     * @param u_old view of the solution field in the old dw
     * @param[out] A view of the implicit matrix stencil field in the new dw
     * @param[out] A_additional view of the implicit matrix non-stencil field in the new dw
     * @param[out] b view of the implicit vector field in the new dw
     */
    void
    time_advance_solution_backward_euler_assemble_hypresstruct_full (
        const IntVector & id,
        const FDView < ScalarField<const double>, STN > & u_old,
        View < ScalarField<Stencil7> > & A,
        HypreSStruct::AdditionalEntries * A_additional,
        View < ScalarField<double> > & b
    );

    /**
     * @brief Assemble hypre sstruct system implementation (Backward Euler, rhs implementation)
     *
     * compute only implicit vector entries at a given grid
     * position using the value of the solution and at previous timestep
     *
     * @param id grid index
     * @param u_old view of the solution field in the old dw
     * @param[out] b view of the implicit vector field in the new dw
     */
    void
    time_advance_solution_backward_euler_assemble_hypresstruct_rhs (
        const IntVector & id,
        const FDView < ScalarField<const double>, STN > & u_old,
        View < ScalarField<double> > & b
    );

    /**
     * @brief Assemble hypre system implementation (Crank Nicolson, full implementation)
     *
     * compute both implicit matrix stencil and vector entries at a given grid
     * position using the value of the solution and at previous timestep
     *
     * @param id grid index
     * @param u_old view of the solution field in the old dw
     * @param[out] A view of the implicit matrix stencil field in the new dw
     * @param[out] b view of the implicit vector field in the new dw
     */
    void
    time_advance_solution_crank_nicolson_assemble_hypre_full (
        const IntVector & id,
        const FDView < ScalarField<const double>, STN > & u_old,
        View < ScalarField<Stencil7> > & A,
        View < ScalarField<double> > & b
    );

    /**
     * @brief Assemble hypre system implementation (Crank Nicolson, rhs implementation)
     *
     * compute only implicit vector entries at a given grid
     * position using the value of the solution and at previous timestep
     *
     * @param id grid index
     * @param u_old view of the solution field in the old dw
     * @param[out] b view of the implicit vector field in the new dw
     */
    void
    time_advance_solution_crank_nicolson_assemble_hypre_rhs (
        const IntVector & id,
        const FDView < ScalarField<const double>, STN > & u_old,
        View < ScalarField<double> > & b
    );

    /**
     * @brief Assemble hypre sstruct system implementation (Crank Nicolson, full implementation)
     *
     * compute both implicit matrix stencil and vector entries at a given grid
     * position using the value of the solution and at previous timestep
     *
     * @param id grid index
     * @param u_old view of the solution field in the old dw
     * @param[out] A view of the implicit matrix stencil field in the new dw
     * @param[out] A_additional view of the implicit matrix non-stencil field in the new dw
     * @param[out] b view of the implicit vector field in the new dw
     */
    void
    time_advance_solution_crank_nicolson_assemble_hypresstruct_full (
        const IntVector & id,
        const FDView < ScalarField<const double>, STN > & u_old,
        View < ScalarField<Stencil7> > & A,
        HypreSStruct::AdditionalEntries * A_additional,
        View < ScalarField<double> > & b
    );

    /**
     * @brief Assemble hypre sstruct system implementation (Crank Nicolson, rhs implementation)
     *
     * compute only implicit vector entries at a given grid
     * position using the value of the solution and at previous timestep
     *
     * @param id grid index
     * @param u_old view of the solution field in the old dw
     * @param[out] b view of the implicit vector field in the new dw
     */
    void
    time_advance_solution_crank_nicolson_assemble_hypresstruct_rhs (
        const IntVector & id,
        const FDView < ScalarField<const double>, STN > & u_old,
        View < ScalarField<double> > & b
    );

    /**
     * @brief Update stencil entries debugging views implementation
     *
     * Copy new value of the debugging views from the implicit matrix stencil at
     * a given grid position
     *
     * @param id grid index
     * @param A view of the implicit matrix stencil field in the new dw
     * @param[out] Ap view of the diagonal stencil entry in the new dw
     * @param[out] Aw view of the west stencil entry in the new dw
     * @param[out] Ae view of the east stencil entry in the new dw
     * @param[out] As view of the south stencil entry in the new dw
     * @param[out] An view of the north stencil entry in the new dw
     * @param[out] Ab view of the bottom stencil entry in the new dw
     * @param[out] At view of the top stencil entry in the new dw
     */
#   ifdef PhaseField_Heat_DBG_MATRIX
    void
    time_advance_update_dbg_matrix (
        const IntVector & id,
        View < ScalarField<const Stencil7> > & A,
        View < ScalarField<double> > & Ap,
        View < ScalarField<double> > & Aw,
        View < ScalarField<double> > & Ae,
        View < ScalarField<double> > & As,
        View < ScalarField<double> > & An,
        View < ScalarField<double> > & Ab,
        View < ScalarField<double> > & At
    );
#   endif
#endif // HAVE_HYPRE

    /**
     * @brief Advance solution epsilon task
     * (test, coarsest level implementation)
     *
     * compute error in u approximation at a given grid position using the
     * analytical solution
     *
     * @param id grid index
     * @param patch grid patch
     * @param ut \f$ \exp ( -\alpha d a^2 t ) \f$;
     * @param a \f$ \pi/2 L$ \f$
     * @param u view of the newly computed solution field in the new dw
     * @param[out] epsilon_u view of the local residual (difference between computed and
     * analytical solution at each grid position)
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    time_advance_solution_epsilon (
        const IntVector & id,
        const Patch * patch,
        const double & ut,
        const double & a,
        const View < ScalarField<const double> > & u,
        View < ScalarField<double> > & epsilon_u
    );

    /**
     * @brief Advance solution epsilon task
     * (test, coarsest level implementation)
     *
     * compute error in u approximation at a given grid position using the
     * analytical solution
     *
     * @param id grid index
     * @param patch grid patch
     * @param ut \f$ \exp ( -\alpha d a^2 t ) \f$;
     * @param a \f$ \pi/2 L$ \f$
     * @param u view of the newly computed solution field in the new dw
     * @param[out] epsilon_u view of the local residual (difference between computed and
     * analytical solution at each grid position)
     *
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    time_advance_solution_epsilon (
        const IntVector & id,
        const Patch * patch,
        const double & ut,
        const double & a,
        const View < ScalarField<const double> > & u,
        View < ScalarField<double> > & epsilon_u
    );

    /**
     * @brief Advance solution error task
     * (test, coarsest level implementation)
     *
     * compute error in u approximation at a given grid position using the
     * analytical solution
     *
     * @param id grid index
     * @param patch grid patch
     * @param ut \f$ \exp ( -\alpha d a^2 t ) \f$;
     * @param a \f$ \pi/2 L$ \f$
     * @param area domain area
     * @param factor weight factor for discrete integrals
     * @param u view of the newly computed solution field in the new dw
     * @param[out] error_u interpolation error (L2 norm over the range of each
     * grid position of the difference between computed and
     * analytical solution at each grid position)
     * @param[out] u_norm2_L2 square of L2-norm (global) of the solution vector
     * @param[out] error_norm2_L2 square of L2-norm (global) of the solution error vector
     *
     */
    template < bool MG >
    typename std::enable_if < !MG, void >::type
    time_advance_solution_error (
        const IntVector & id,
        const Patch * patch,
        const double & ut,
        const double & a,
        const double & area,
        const double & factor,
        const View < ScalarField<const double> > & u,
        View < ScalarField<double> > & error_u,
        double & u_norm2_L2,
        double & error_norm2_L2
    );

    /**
     * @brief Advance solution error task
     * (test, refined level implementation)
     *
     * compute error in u approximation at a given grid position using the
     * analytical solution
     *
     * @param id grid index
     * @param patch grid patch
     * @param patch_finest reference grid patch
     * @param refinement grid refinement factors
     * @param ut \f$ \exp ( -\alpha d a^2 t ) \f$;
     * @param a \f$ \pi/2 L$ \f$
     * @param area domain area
     * @param factor weight factor for discrete integrals
     * @param u_finest view for evaluating the solution on the reference patch (interpolator)
     * @param[out] error_u interpolation error (L2 norm over the range of each
     * grid position of the difference between computed and
     * analytical solution at each grid position)
     * @param[out] u_norm2_L2 square of L2-norm (global) of the solution vector
     * @param[out] error_norm2_L2 square of L2-norm (global) of the solution error vector
     *
     */
    template < bool MG >
    typename std::enable_if < MG, void >::type
    time_advance_solution_error (
        const IntVector & id,
        const Patch * patch,
        const Patch * patch_finest,
        const IntVector & refinement,
        const double & ut,
        const double & a,
        const double & area,
        const double & factor,
        const View < ScalarField<const double> > & u_finest,
        View < ScalarField<double> > & error_u,
        double & u_norm2_L2,
        double & error_norm2_L2
    );

    /**
     * @brief Refine solution implementation
     *
     * Computes interpolated value of u at a given grid position

     * @param id_fine fine grid index
     * @param u_coarse_interp interpolator of the aolution field on the coarse level
     * @param[out] u_fine view of the aolution field on the fine level
     */
    void
    refine_solution (
        const IntVector id_fine,
        const View < ScalarField<const double> > & u_coarse_interp,
        View < ScalarField<double> > & u_fine
    );

    /**
     * @brief Coarsen solution implementation
     *
     * Computes restricted value of u at a given grid position

     * @param id_coarse coarse grid index
     * @param u_fine_restr restrictor of the aolution field on the fine level
     * @param[out] u_coarse view of the aolution field on the coarse level
     */
    void
    coarsen_solution (
        const IntVector id_coarse,
        const View < ScalarField<const double> > & u_fine_restr,
        View < ScalarField<double> > & u_coarse
    );

    /**
     * @brief ErrorEstimate solution implementation (cell centered implementation)
     *
     * Computes the gradient of the phase field using its value at the previous
     * timestep and set refinement flag where it is above the threshold given
     * in input
     *
     * @param id grid index
     * @param u view of the solution the old dw
     * @param[out] refine_flag view of refine flag (grid field) in the new dw
     */
    template < VarType V >
    typename std::enable_if < V == CC, void >::type
    error_estimate_solution (
        const IntVector & id,
        const double & r,
        FDView < ScalarField<const double>, STN > & u,
        View < ScalarField<int> > & refine_flag
    );

    /**
     * @brief ErrorEstimate solution implementation (vertex based implementation)
     *
     * Computes the gradient of the phase field using its value at the previous
     * timestep and set refinement flag where it is above the threshold given
     * in input
     *
     * @param id grid index
     * @param u view of the solution in the old dw
     * @param[out] refine_flag view of refine flag (grid field) in the new dw
     */
    template < VarType V >
    typename std::enable_if < V == NC, void >::type
    error_estimate_solution (
        const IntVector & id,
        const double & r,
        FDView < ScalarField<const double>, STN > & u,
        View < ScalarField<int> > & refine_flag
    );

}; // class Heat

// CONSTRUCTORS/DESTRUCTOR

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
Heat<VAR, DIM, STN, AMR, TST>::Heat (
    const ProcessorGroup * myWorld,
    const MaterialManagerP materialManager,
    int verbosity
) : Application< HeatProblem<VAR, STN, TST>, AMR > ( myWorld, materialManager, verbosity )
{
    u_label = VarLabel::create ( "u", Variable<VAR, double>::getTypeDescription() );
    u_norm2_L2_label = VarLabel::create ( "u_norm2_L2", sum_vartype::getTypeDescription() );

    if ( TST )
    {
        epsilon_u_label = VarLabel::create ( "epsilon_u", Variable<VAR, double>::getTypeDescription() );
        error_u_label = VarLabel::create ( "error_u", Variable<CC, double>::getTypeDescription() );
        error_norm2_L2_label = VarLabel::create ( "error_norm2_L2", sum_vartype::getTypeDescription() );
    }

#ifdef HAVE_HYPRE
    matrix_label = VarLabel::create ( "A", Variable<VAR, Stencil7>::getTypeDescription() );
    additional_entries_label = VarLabel::create ( "A" + HypreSStruct::Solver<DIM>::AdditionalEntriesSuffix, _AdditionalEntries::getTypeDescription() );
    rhs_label = VarLabel::create ( "b", Variable<VAR, double>::getTypeDescription() );
#   ifdef PhaseField_Heat_DBG_MATRIX
    Ap_label = VarLabel::create ( "Ap", Variable<VAR, double>::getTypeDescription() );
    Aw_label = VarLabel::create ( "Aw", Variable<VAR, double>::getTypeDescription() );
    Ae_label = VarLabel::create ( "Ae", Variable<VAR, double>::getTypeDescription() );
    An_label = VarLabel::create ( "An", Variable<VAR, double>::getTypeDescription() );
    As_label = VarLabel::create ( "As", Variable<VAR, double>::getTypeDescription() );
    At_label = VarLabel::create ( "At", Variable<VAR, double>::getTypeDescription() );
    Ab_label = VarLabel::create ( "Ab", Variable<VAR, double>::getTypeDescription() );
#   endif
#endif

#ifdef PhaseField_Heat_DBG_DERIVATIVES
    du_label[X] = VarLabel::create ( "ux", Variable<VAR, double>::getTypeDescription() );
    ddu_label[X] = VarLabel::create ( "uxx", Variable<VAR, double>::getTypeDescription() );
    if ( TST )
    {
        epsilon_du_label[X] = VarLabel::create ( "epsilon_ux", Variable<VAR, double>::getTypeDescription() );
        epsilon_ddu_label[X] = VarLabel::create ( "epsilon_uxx", Variable<VAR, double>::getTypeDescription() );
        error_du_label[X] = VarLabel::create ( "error_ux", Variable<CC, double>::getTypeDescription() );
        error_ddu_label[X] = VarLabel::create ( "error_uxx", Variable<CC, double>::getTypeDescription() );
    }
    if ( DIM > D1 )
    {
        du_label[Y] = VarLabel::create ( "uy", Variable<VAR, double>::getTypeDescription() );
        ddu_label[Y] = VarLabel::create ( "uyy", Variable<VAR, double>::getTypeDescription() );
        if ( TST )
        {
            epsilon_du_label[Y] = VarLabel::create ( "epsilon_uy", Variable<VAR, double>::getTypeDescription() );
            epsilon_ddu_label[Y] = VarLabel::create ( "epsilon_uyy", Variable<VAR, double>::getTypeDescription() );
            error_du_label[Y] = VarLabel::create ( "error_uy", Variable<CC, double>::getTypeDescription() );
            error_ddu_label[Y] = VarLabel::create ( "error_uyy", Variable<CC, double>::getTypeDescription() );
        }
    }
    if ( DIM > D2 )
    {
        du_label[Z] = VarLabel::create ( "uz", Variable<VAR, double>::getTypeDescription() );
        ddu_label[Z] = VarLabel::create ( "uzz", Variable<VAR, double>::getTypeDescription() );
        if ( TST )
        {
            epsilon_du_label[Z] = VarLabel::create ( "epsilon_uz", Variable<VAR, double>::getTypeDescription() );
            epsilon_ddu_label[Z] = VarLabel::create ( "epsilon_uzz", Variable<VAR, double>::getTypeDescription() );
            error_du_label[Z] = VarLabel::create ( "error_uz", Variable<CC, double>::getTypeDescription() );
            error_ddu_label[Z] = VarLabel::create ( "error_uzz", Variable<CC, double>::getTypeDescription() );
        }
    }
    u_norm2_H10_label = VarLabel::create ( "u_norm2_H10", sum_vartype::getTypeDescription() );
    u_norm2_H20_label = VarLabel::create ( "u_norm2_H20", sum_vartype::getTypeDescription() );
    if ( TST )
    {
        error_norm2_H10_label = VarLabel::create ( "error_norm2_H10", sum_vartype::getTypeDescription() );
        error_norm2_H20_label = VarLabel::create ( "error_norm2_H20", sum_vartype::getTypeDescription() );
    }
#endif
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
Heat<VAR, DIM, STN, AMR, TST>::~Heat()
{
    VarLabel::destroy ( u_label );
    VarLabel::destroy ( u_norm2_L2_label );
    if ( TST )
    {
        VarLabel::destroy ( epsilon_u_label );
        VarLabel::destroy ( error_u_label );
        VarLabel::destroy ( error_norm2_L2_label );
    }
#ifdef HAVE_HYPRE
    VarLabel::destroy ( matrix_label );
    VarLabel::destroy ( additional_entries_label );
    VarLabel::destroy ( rhs_label );
#   ifdef PhaseField_Heat_DBG_MATRIX
    VarLabel::destroy ( Ap_label );
    VarLabel::destroy ( Aw_label );
    VarLabel::destroy ( Ae_label );
    VarLabel::destroy ( An_label );
    VarLabel::destroy ( As_label );
    VarLabel::destroy ( At_label );
    VarLabel::destroy ( Ab_label );
#   endif
#endif
#ifdef PhaseField_Heat_DBG_DERIVATIVES
    for ( size_t D = 0; D < DIM; ++D )
    {
        VarLabel::destroy ( du_label[D] );
        VarLabel::destroy ( ddu_label[D] );
        if ( TST )
        {
            VarLabel::destroy ( epsilon_du_label[D] );
            VarLabel::destroy ( epsilon_ddu_label[D] );
            VarLabel::destroy ( error_du_label[D] );
            VarLabel::destroy ( error_ddu_label[D] );
        }
    }
    VarLabel::destroy ( u_norm2_H10_label );
    VarLabel::destroy ( u_norm2_H20_label );
    if ( TST )
    {
        VarLabel::destroy ( error_norm2_H10_label );
        VarLabel::destroy ( error_norm2_H20_label );
    }
#endif
}

// SETUP

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::problemSetup (
    const ProblemSpecP & params,
    const ProblemSpecP &,
    GridP &
)
{
    this->m_materialManager->registerSimpleMaterial ( scinew SimpleMaterial() );

    ProblemSpecP heat = params->findBlock ( "PhaseField" );
    heat->require ( "delt", delt );
    heat->require ( "alpha", alpha );

    std::string scheme;
    heat->getWithDefault ( "scheme", scheme, "forward_euler" );
#ifdef HAVE_HYPRE
    time_scheme = str_to_ts ( scheme );

    if ( VAR == NC )
    {
        if ( time_scheme & TS::Implicit )
            SCI_THROW ( InternalError ( "\n ERROR: implicit solver not implemented for node centered variables", __FILE__, __LINE__ ) );
    }

    if ( time_scheme & TS::Implicit )
    {
        ProblemSpecP solv = params->findBlock ( "Solver" );
        this->m_solver = dynamic_cast<SolverInterface *> ( this->getPort ( "solver" ) );
        if ( !this->m_solver )
        {
            SCI_THROW ( InternalError ( "Heat:couldn't get solver port", __FILE__, __LINE__ ) );
        }
        this->m_solver->readParameters ( solv, "u" );
        this->m_solver->getParameters()->setSymmetric ( false );
        this->m_solver->getParameters()->setSolveOnExtraCells ( false );
    }
#else
    if ( scheme != "forward_euler" )
        SCI_THROW ( InternalError ( "\n ERROR: Implicit time scheme requires HYPRE\n", __FILE__, __LINE__ ) );
#endif

    problemSetup_boundary_variables<TST>();

    if ( AMR )
    {
        this->setLockstepAMR ( true );

        // read amr parameters
        heat->require ( "refine_threshold", refine_threshold );

        std::map<std::string, FC> c2f;

        // default values
        c2f[u_label->getName()] = ( VAR == CC ) ? FC::FC0 : FC::FC1;

        ProblemSpecP amr, regridder, fci;
        if ( ! ( amr = params->findBlock ( "AMR" ) ) ) return;
        if ( ! ( fci = amr->findBlock ( "FineCoarseInterfaces" ) ) ) return;
        if ( ! ( fci = fci->findBlock ( "FCIType" ) ) ) return;
        do
        {
            std::string label, var;
            fci->getAttribute ( "label", label );
            fci->getAttribute ( "var", var );
            c2f[label] = str_to_fc ( var );
        }
        while ( ( fci = fci->findNextBlock ( "FCIType" ) ) );

        this->setC2F ( c2f );
    }
}

// SCHEDULINGS

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleInitialize (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleInitialize on level " << level->getIndex() << " " );

    scheduleInitialize_solution<AMR> ( level, sched );
    scheduleTimeAdvance_solution_error<AMR, TST> ( level, sched );

#ifdef PhaseField_Heat_DBG_DERIVATIVES
    scheduleInitialize_dbg_derivatives<AMR> ( level, sched );
    scheduleTimeAdvance_dbg_derivatives_error<AMR, TST> ( level, sched );
#endif
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleInitialize_solution (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleInitialize_solution on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_initialize_solution", this, &Heat::task_initialize_solution );
    task->computes ( u_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

/**
 * @remark we need to schedule all levels before task_error_estimate_solution to avoid
 * the error "Failure finding [u , coarseLevel, MI: none, NewDW
 * (mapped to dw index 1), ####] for Heat::task_error_estimate_solution",
 * on patch #, Level #, on material #, resource (rank): #" while compiling the
 * TaskGraph
 */
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleInitialize_solution (
    const LevelP & level,
    SchedulerP & sched
)
{
    // since the SimulationController is calling this scheduler starting from
    // the finest level we schedule only on the finest level
    if ( level->hasFinerLevel() ) return;

    DOUTR ( dbg_heat_scheduling, "scheduleInitialize_solution on all levels " );

    GridP grid = level->getGrid();
    for ( int l = 0; l < grid->numLevels(); ++l )
        scheduleInitialize_solution < !MG > ( grid->getLevel ( l ), sched );
}

#ifdef PhaseField_Heat_DBG_DERIVATIVES
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleInitialize_dbg_derivatives (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleInitialize_dbg_derivatives on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_dbg_derivatives", this, &Heat::task_time_advance_dbg_derivatives );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::NewDW, u_label, FGT, FGN );
    for ( size_t D = 0; D < DIM; ++D )
    {
        task->computes ( du_label[D] );
        task->computes ( ddu_label[D] );
    }
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleInitialize_dbg_derivatives (
    const LevelP & level,
    SchedulerP & sched
)
{
    // all tasks must be sent to the scheduler before the first dbg_derivatives_error task
    if ( level->hasFinerLevel() ) return;

    DOUTR ( dbg_heat_scheduling, "scheduleInitialize_dbg_derivatives on all levels " );
    GridP grid = level->getGrid();

    scheduleInitialize_dbg_derivatives < !MG > ( grid->getLevel ( 0 ), sched );
    for ( int l = 1; l < grid->numLevels(); ++l )
    {
        DOUTR ( dbg_heat_scheduling, "scheduleInitialize_dbg_derivatives on level " << l << " " );

        Task * task = scinew Task ( "Heat::task_time_advance_dbg_derivatives", this, &Heat::task_time_advance_dbg_derivatives );

        task->requires ( Task::NewDW, this->getSubProblemsLabel(), FGT, FGN );
        task->requires ( Task::NewDW, this->getSubProblemsLabel(), nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
        task->requires ( Task::NewDW, u_label, FGT, FGN );
        task->requires ( Task::NewDW, u_label, nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );

        for ( size_t D = 0; D < DIM; ++D )
        {
            task->computes ( du_label[D] );
            task->computes ( ddu_label[D] );
        }
        sched->addTask ( task, grid->getLevel ( l )->eachPatch(), this->m_materialManager->allMaterials() );
    }
}
#endif // PhaseField_Heat_DBG_DERIVATIVES

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleRestartInitialize (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleRestartInitialize on level " << level->getIndex() << " " );

    if ( time_scheme & TS::Implicit && this->m_solver->getName() == "hypre_sstruct" )
    {
        Task * task = scinew Task ( "Heat::task_restart_initialize_hypresstruct", this, &Heat::task_restart_initialize_hypresstruct );
        task->computes ( matrix_label );
        task->computes ( additional_entries_label );
        sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
    }
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleComputeStableTimeStep (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleComputeStableTimeStep on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_compute_stable_timestep ", this, &Heat::task_compute_stable_timestep );
    if ( TST ) task->requires ( Task::OldDW, u_norm2_L2_label );
    task->computes ( this->getDelTLabel(), level.get_rep() );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );

    is_time_advance_solution_scheduled = false;
    is_time_advance_solution_error_scheduled = false;
    is_time_advance_dbg_derivatives_scheduled = false;
    is_time_advance_dbg_derivatives_error_scheduled = false;
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance on level " << level->getIndex() << " " );

    scheduleTimeAdvance_solution ( level, sched );
    scheduleTimeAdvance_solution_error<AMR, TST> ( level, sched );

#ifdef PhaseField_Heat_DBG_DERIVATIVES
    scheduleTimeAdvance_dbg_derivatives<AMR> ( level, sched );
    scheduleTimeAdvance_dbg_derivatives_error<AMR, TST> ( level, sched );
#endif
};

#ifdef PhaseField_Heat_DBG_DERIVATIVES
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_dbg_derivatives (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_dbg_derivatives on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_dbg_derivatives", this, &Heat::task_time_advance_dbg_derivatives );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::NewDW, u_label, FGT, FGN );
    for ( size_t D = 0; D < DIM; ++D )
    {
        task->computes ( du_label[D] );
        task->computes ( ddu_label[D] );
    }
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_dbg_derivatives (
    const LevelP & level,
    SchedulerP & sched
)
{
    // tasks must be sent to the scheduler after the last solution task
    if ( is_time_advance_dbg_derivatives_scheduled ) return;

    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_dbg_derivatives on all levels " );
    GridP grid = level->getGrid();

    scheduleTimeAdvance_dbg_derivatives < !MG > ( grid->getLevel ( 0 ), sched );
    for ( int l = 1; l < grid->numLevels(); ++l )
    {
        DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_dbg_derivatives on level " << l << " " );

        Task * task = scinew Task ( "Heat::task_time_advance_dbg_derivatives", this, &Heat::task_time_advance_dbg_derivatives );

        task->requires ( Task::NewDW, this->getSubProblemsLabel(), FGT, FGN );
        task->requires ( Task::NewDW, this->getSubProblemsLabel(), nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
        task->requires ( Task::NewDW, u_label, FGT, FGN );
        task->requires ( Task::NewDW, u_label, nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );

        for ( size_t D = 0; D < DIM; ++D )
        {
            task->computes ( du_label[D] );
            task->computes ( ddu_label[D] );
        }
        sched->addTask ( task, grid->getLevel ( l )->eachPatch(), this->m_materialManager->allMaterials() );
    }

    is_time_advance_dbg_derivatives_scheduled = true;
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG, bool T >
typename std::enable_if < T && !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_dbg_derivatives_error (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_dbg_derivatives_error on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_dbg_derivatives_error", this, &Heat::task_time_advance_dbg_derivatives_error<MG, T> );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), IGT, IGN );
    for ( size_t D = 0; D < DIM; ++D )
    {
        task->requires ( Task::NewDW, du_label[D], IGT, IGN );
        task->requires ( Task::NewDW, ddu_label[D], IGT, IGN );
        task->computes ( epsilon_du_label[D] );
        task->computes ( epsilon_ddu_label[D] );
        task->computes ( error_du_label[D] );
        task->computes ( error_ddu_label[D] );
    }
    task->computes ( u_norm2_H10_label );
    task->computes ( u_norm2_H20_label );
    task->computes ( error_norm2_H10_label );
    task->computes ( error_norm2_H20_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG, bool T >
typename std::enable_if < T && MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_dbg_derivatives_error (
    const LevelP & level,
    SchedulerP & sched
)
{
    // all tasks must be sent to the scheduler after the last task_time_advance_solution
    if ( is_time_advance_dbg_derivatives_error_scheduled ) return;

    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_dbg_derivatives_error on all levels " );

    GridP grid = level->getGrid();

    scheduleTimeAdvance_dbg_derivatives_error < !MG, T > ( grid->getLevel ( 0 ), sched );
    for ( int l = 1; l < grid->numLevels(); ++l )
    {
        DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_dbg_derivatives_error on level " << level->getIndex() << " " );

        Task * task = scinew Task ( "Heat::task_time_advance_dbg_derivatives_error", this, &Heat::task_time_advance_dbg_derivatives_error<MG, T> );
        task->requires ( Task::NewDW, this->getSubProblemsLabel(), IGT, IGN );

        for ( size_t D = 0; D < DIM; ++D )
        {
            task->requires ( Task::NewDW, du_label[D], IGT, IGN );
            task->requires ( Task::NewDW, ddu_label[D], IGT, IGN );
            task->computes ( epsilon_du_label[D] );
            task->computes ( epsilon_ddu_label[D] );
            task->computes ( error_du_label[D] );
            task->computes ( error_ddu_label[D] );
        }
        task->computes ( u_norm2_H10_label );
        task->computes ( u_norm2_H20_label );
        task->computes ( error_norm2_H10_label );
        task->computes ( error_norm2_H20_label );
        sched->addTask ( task, grid->getLevel ( l )->eachPatch(), this->m_materialManager->allMaterials() );
    }

    is_time_advance_dbg_derivatives_error_scheduled = true;
}
#endif

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution on level " << level->getIndex() << " " );

#ifdef HAVE_HYPRE
    if ( time_scheme == TS::ForwardEuler )
#endif
        scheduleTimeAdvance_solution_forward_euler<AMR> ( level, sched );
#ifdef HAVE_HYPRE
    else
        scheduleTimeAdvance_solution_implicit<AMR> ( level, sched );
#endif
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_forward_euler (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_forward_euler on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_solution_forward_euler", this, &Heat::task_time_advance_solution_forward_euler );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::OldDW, u_label, FGT, FGN );
    task->computes ( u_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_forward_euler (
    const LevelP & level,
    SchedulerP & sched
)
{
    // all tasks must be sent to the scheduler before the first dbg_derivatives_error task
    if ( is_time_advance_solution_scheduled ) return;

    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_forward_euler on all levels " );
    GridP grid = level->getGrid();

    scheduleTimeAdvance_solution_forward_euler < !MG > ( grid->getLevel ( 0 ), sched );
    for ( int l = 1; l < grid->numLevels(); ++l )
    {
        Task * task = scinew Task ( "Heat::task_time_advance_solution_forward_euler", this, &Heat::task_time_advance_solution_forward_euler );
        task->requires ( Task::OldDW, this->getSubProblemsLabel(), FGT, FGN );
        task->requires ( Task::OldDW, this->getSubProblemsLabel(), nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
        task->requires ( Task::OldDW, u_label, FGT, FGN );
        task->requires ( Task::OldDW, u_label, nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
        task->computes ( u_label );
        sched->addTask ( task, grid->getLevel ( l )->eachPatch(), this->m_materialManager->allMaterials() );
    }

    is_time_advance_solution_scheduled = true;
}

#ifdef HAVE_HYPRE
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_implicit
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_implicit on level " << level->getIndex() << " " );

    void ( Heat::*scheduleTimeAdvance_assemble ) ( const LevelP &, SchedulerP & );

    switch ( time_scheme )
    {
    case TS::BackwardEuler:
        scheduleTimeAdvance_assemble = &Heat::scheduleTimeAdvance_solution_backward_euler_assemble_hypre<AMR>;
        break;
    case TS::CrankNicolson:
        scheduleTimeAdvance_assemble = &Heat::scheduleTimeAdvance_solution_crank_nicolson_assemble_hypre<AMR>;
        break;
    default:
        SCI_THROW ( InternalError ( "\n ERROR: Unknown time scheme\n", __FILE__, __LINE__ ) );
    }

    ( this->*scheduleTimeAdvance_assemble ) ( level, sched );

    this->m_solver->scheduleSolve ( level, sched,
                                    this->m_materialManager->allMaterials(),
                                    matrix_label, Task::NewDW, // A
                                    u_label, false,            // x
                                    rhs_label, Task::NewDW,    // b
                                    u_label, Task::OldDW       // guess
                                  );

#   ifdef PhaseField_Heat_DBG_MATRIX
    scheduleTimeAdvance_update_dbg_matrix ( level, sched );
#   endif
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_implicit
(
    const LevelP & level,
    SchedulerP & sched
)
{
    if ( is_time_advance_solution_scheduled ) return;

    if ( this->m_solver->getName() == "hypre_sstruct" )
    {
        void ( Heat::*scheduleTimeAdvance_assemble_sg ) ( const LevelP &, SchedulerP & );
        void ( Heat::*scheduleTimeAdvance_assemble_mg ) ( const LevelP &, SchedulerP & );

        switch ( time_scheme )
        {
        case TS::BackwardEuler:
            scheduleTimeAdvance_assemble_sg = &Heat::scheduleTimeAdvance_solution_backward_euler_assemble_hypresstruct < !MG >;
            scheduleTimeAdvance_assemble_mg = &Heat::scheduleTimeAdvance_solution_backward_euler_assemble_hypresstruct<MG>;
            break;
        case TS::CrankNicolson:
            scheduleTimeAdvance_assemble_sg = &Heat::scheduleTimeAdvance_solution_crank_nicolson_assemble_hypresstruct < !MG >;
            scheduleTimeAdvance_assemble_mg = &Heat::scheduleTimeAdvance_solution_crank_nicolson_assemble_hypresstruct<MG>;
            break;
        default:
            SCI_THROW ( InternalError ( "\n ERROR: Unknown time scheme\n", __FILE__, __LINE__ ) );
        }

        DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_implicit on all levels " );
        GridP grid = level->getGrid();

        ( this->*scheduleTimeAdvance_assemble_sg ) ( grid->getLevel ( 0 ), sched );

        for ( int l = 1; l < grid->numLevels(); ++l )
            ( this->*scheduleTimeAdvance_assemble_mg ) ( grid->getLevel ( l ), sched );

        for ( int l = 0; l < grid->numLevels(); ++l )
            scheduleTimeAdvannce_comunicate_before_solve_hypresstruct ( grid->getLevel ( l ), sched );

        this->m_solver->scheduleSolve ( level, sched, this->m_materialManager->allMaterials(),
                                        matrix_label, Task::NewDW, // A
                                        u_label, false,            // x
                                        rhs_label, Task::NewDW,    // b
                                        u_label, Task::OldDW       // guess
                                      );
#   ifdef PhaseField_Heat_DBG_MATRIX
        for ( int l = 0; l < grid->numLevels(); ++l )
            scheduleTimeAdvance_update_dbg_matrix ( grid->getLevel ( l ), sched );
#   endif
    }
    else
    {
        void ( Heat::*scheduleTimeAdvance_assemble_sg ) ( const LevelP &, SchedulerP & );
        void ( Heat::*scheduleTimeAdvance_assemble_mg ) ( const LevelP &, SchedulerP & );

        switch ( time_scheme )
        {
        case TS::BackwardEuler:
            scheduleTimeAdvance_assemble_sg = &Heat::scheduleTimeAdvance_solution_backward_euler_assemble_hypre < !MG >;
            scheduleTimeAdvance_assemble_mg = &Heat::scheduleTimeAdvance_solution_backward_euler_assemble_hypre < MG >;
            break;
        case TS::CrankNicolson:
            scheduleTimeAdvance_assemble_sg = &Heat::scheduleTimeAdvance_solution_crank_nicolson_assemble_hypre < !MG >;
            scheduleTimeAdvance_assemble_mg = &Heat::scheduleTimeAdvance_solution_crank_nicolson_assemble_hypre < MG >;
            break;
        default:
            SCI_THROW ( InternalError ( "\n ERROR: Unknown time scheme\n", __FILE__, __LINE__ ) );
        }

        GridP grid = level->getGrid();

        ( this->*scheduleTimeAdvance_assemble_sg ) ( grid->getLevel ( 0 ), sched );

        this->m_solver->scheduleSolve ( grid->getLevel ( 0 ), sched,
                                        this->m_materialManager->allMaterials(),
                                        matrix_label, Task::NewDW, // A
                                        u_label, false,            // x
                                        rhs_label, Task::NewDW,    // b
                                        u_label, Task::OldDW       // guess
                                      );

#   ifdef PhaseField_Heat_DBG_MATRIX
        scheduleTimeAdvance_update_dbg_matrix ( grid->getLevel ( 0 ), sched );
#   endif

        for ( int l = 1; l < grid->numLevels(); ++l )
        {
            ( this->*scheduleTimeAdvance_assemble_mg ) ( grid->getLevel ( l ), sched );

            this->m_solver->scheduleSolve ( grid->getLevel ( l ), sched,
                                            this->m_materialManager->allMaterials(),
                                            matrix_label, Task::NewDW, // A
                                            u_label, false,            // x
                                            rhs_label, Task::NewDW,    // b
                                            u_label, Task::OldDW       // guess
                                          );

#   ifdef PhaseField_Heat_DBG_MATRIX
            scheduleTimeAdvance_update_dbg_matrix ( grid->getLevel ( l ), sched );
#   endif
        }
    }

    is_time_advance_solution_scheduled = true;
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_backward_euler_assemble_hypre
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_backward_euler_assemble_hypre on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_solution_backward_euler_assemble_hypre", this, &Heat::task_time_advance_solution_backward_euler_assemble_hypre );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::OldDW, u_label, FGT, FGN );
    task->requires ( Task::OldDW, matrix_label, Ghost::None, 0 );
    task->computes ( matrix_label );
    task->computes ( rhs_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_backward_euler_assemble_hypre
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_backward_euler_assemble_hypre on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_solution_backward_euler_assemble_hypre", this, &Heat::task_time_advance_solution_backward_euler_assemble_hypre );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    task->requires ( Task::OldDW, u_label, FGT, FGN );
    task->requires ( Task::NewDW, u_label, nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    task->requires ( Task::OldDW, matrix_label, Ghost::None, 0 );
    task->computes ( matrix_label );
    task->computes ( rhs_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_backward_euler_assemble_hypresstruct
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_backward_euler_assemble_hypresstruct on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_solution_backward_euler_assemble_hypresstruct", this, &Heat::task_time_advance_solution_backward_euler_assemble_hypresstruct );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::OldDW, u_label, FGT, FGN );
    task->requires ( Task::OldDW, matrix_label, Ghost::None, 0 );
    task->requires ( Task::OldDW, additional_entries_label, Ghost::None, 0 );
    task->computes ( matrix_label );
    task->computes ( additional_entries_label );
    task->computes ( rhs_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_backward_euler_assemble_hypresstruct
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_backward_euler_assemble_hypresstruct on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_solution_backward_euler_assemble_hypresstruct", this, &Heat::task_time_advance_solution_backward_euler_assemble_hypresstruct );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    task->requires ( Task::OldDW, u_label, FGT, FGN );
    task->requires ( Task::OldDW, u_label, nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    task->requires ( Task::OldDW, matrix_label, Ghost::None, 0 );
    task->requires ( Task::OldDW, additional_entries_label, Ghost::None, 0 );
    task->computes ( matrix_label );
    task->computes ( additional_entries_label );
    task->computes ( rhs_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_crank_nicolson_assemble_hypre
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_crank_nicolson_assemble_hypre on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_solution_crank_nicolson_assemble_hypre", this, &Heat::task_time_advance_solution_crank_nicolson_assemble_hypre );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::OldDW, u_label, FGT, FGN );
    task->requires ( Task::OldDW, matrix_label, Ghost::None, 0 );
    task->computes ( matrix_label );
    task->computes ( rhs_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_crank_nicolson_assemble_hypre
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_crank_nicolson_assemble_hypre on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_solution_crank_nicolson_assemble_hypre", this, &Heat::task_time_advance_solution_crank_nicolson_assemble_hypre );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    task->requires ( Task::OldDW, u_label, FGT, FGN );
    task->requires ( Task::NewDW, u_label, nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    task->requires ( Task::OldDW, matrix_label, Ghost::None, 0 );
    task->computes ( matrix_label );
    task->computes ( rhs_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_crank_nicolson_assemble_hypresstruct
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_crank_nicolson_assemble_hypresstruct on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_solution_crank_nicolson_assemble_hypresstruct", this, &Heat::task_time_advance_solution_crank_nicolson_assemble_hypresstruct );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::OldDW, u_label, FGT, FGN );
    task->requires ( Task::OldDW, matrix_label, Ghost::None, 0 );
    task->requires ( Task::OldDW, additional_entries_label, Ghost::None, 0 );
    task->computes ( matrix_label );
    task->computes ( additional_entries_label );
    task->computes ( rhs_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_crank_nicolson_assemble_hypresstruct
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_crank_nicolson_assemble_hypresstruct on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_solution_crank_nicolson_assemble_hypresstruct", this, &Heat::task_time_advance_solution_crank_nicolson_assemble_hypresstruct );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::OldDW, this->getSubProblemsLabel(), nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    task->requires ( Task::OldDW, u_label, FGT, FGN );
    task->requires ( Task::OldDW, u_label, nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    task->requires ( Task::OldDW, matrix_label, Ghost::None, 0 );
    task->requires ( Task::OldDW, additional_entries_label, Ghost::None, 0 );
    task->computes ( matrix_label );
    task->computes ( additional_entries_label );
    task->computes ( rhs_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvannce_comunicate_before_solve_hypresstruct
(
    const LevelP & level,
    SchedulerP & sched
)
{
    // add empty task to force receiving of all pending mpi messages before hypre solve
    Task * task = scinew Task ( "task_communicate_before_solve", this, &Heat::task_communicate_before_solve );

    task->requires ( Task::NewDW, this->getSubProblemsLabel(), FGT, FGN );
    if ( level->hasCoarserLevel() )
        task->requires ( Task::NewDW, this->getSubProblemsLabel(), nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    if ( level->hasFinerLevel() )
        task->requires ( Task::NewDW, this->getSubProblemsLabel(), nullptr, Task::FineLevel, nullptr, Task::NormalDomain, Ghost::None, 0 );

    task->modifies ( matrix_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

#   ifdef PhaseField_Heat_DBG_MATRIX
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_update_dbg_matrix
(
    const LevelP & level,
    SchedulerP & sched
)
{
    Task * task = scinew Task ( "Heat::task_time_advance_update_dbg_matrix", this, &Heat::task_time_advance_update_dbg_matrix );
    // force update after solve
    task->requires ( Task::NewDW, u_label, Ghost::None, 0 );
    task->requires ( Task::NewDW, matrix_label, Ghost::None, 0 );
    task->computes ( Ap_label );
    task->computes ( Aw_label );
    task->computes ( Ae_label );
    task->computes ( An_label );
    task->computes ( As_label );
    task->computes ( At_label );
    task->computes ( Ab_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}
#   endif
#endif // HAVE_HYPRE

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG, bool T >
typename std::enable_if < T && !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_error (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_error on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_time_advance_solution_error", this, &Heat::task_time_advance_solution_error<MG, T> );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), IGT, IGN );
    task->requires ( Task::NewDW, u_label, IGT, IGN );
    task->computes ( epsilon_u_label );
    task->computes ( error_u_label );
    task->computes ( u_norm2_L2_label );
    task->computes ( error_norm2_L2_label );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG, bool T >
typename std::enable_if < T && MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleTimeAdvance_solution_error (
    const LevelP & level,
    SchedulerP & sched
)
{
    // all tasks must be sent to the scheduler after the last task_time_advance_solution
    if ( is_time_advance_solution_error_scheduled ) return;

    DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_error on all levels " );

    GridP grid = level->getGrid();
    int l = grid->numLevels() - 1;

    if ( l == this->m_regridder->maxLevels() - 1 )
    {
        scheduleTimeAdvance_solution_error < !MG, T > ( grid->getLevel ( l ), sched );
        --l;
    }

    for ( ; l >= 0; --l )
    {
        DOUTR ( dbg_heat_scheduling, "scheduleTimeAdvance_solution_error on level " << l << " " );

        Task * task = scinew Task ( "Heat::task_time_advance_solution_error", this, &Heat::task_time_advance_solution_error<MG, T> );
        task->requires ( Task::NewDW, this->getSubProblemsLabel(), IGT, IGN );
        task->requires ( Task::NewDW, u_label, IGT, IGN );
        task->computes ( epsilon_u_label );
        task->computes ( error_u_label );
        task->computes ( u_norm2_L2_label );
        task->computes ( error_norm2_L2_label );
        sched->addTask ( task, grid->getLevel ( l )->eachPatch(), this->m_materialManager->allMaterials() );
    }

    is_time_advance_solution_error_scheduled = true;
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleRefine
(
    const PatchSet * new_patches,
    SchedulerP & sched
)
{
    const Level * level = getLevel ( new_patches );
    DOUTR ( dbg_heat_scheduling, "scheduleRefine on level " << level->getIndex() << " " );

    // no need to refine on coarser level
    if ( level->hasCoarserLevel() )
        scheduleRefine_solution ( new_patches, sched );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleRefine_solution (
    const PatchSet * new_patches,
    SchedulerP & sched
)
{
    const Level * level = getLevel ( new_patches );
    DOUTR ( dbg_heat_scheduling, "scheduleRefine_solution on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_refine_solution", this, &Heat::task_refine_solution );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), Ghost::None, 0 );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    task->requires ( Task::NewDW, u_label, nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
    task->computes ( u_label );

#ifdef HAVE_HYPRE
    /*************************** WORKAROUND ***************************/
    /* on new patches of finer level need to create matrix variables  */
    task->computes ( matrix_label );
    /************************* END WORKAROUND *************************/
#endif

    sched->addTask ( task, new_patches, this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleRefineInterface (
    const LevelP & level_fine,
    SchedulerP & /*sched*/,
    bool /*need_old_coarse*/,
    bool /*need_new_coarse*/
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleRefineInterface on level " << level_fine->getIndex() << " " );
};

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleCoarsen
(
    const LevelP & level_coarse,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleCoarsen on level " << level_coarse->getIndex() << " " );

    scheduleCoarsen_solution ( level_coarse, sched );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleCoarsen_solution (
    const LevelP & level_coarse,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleCoarsen_solution on level " << level_coarse->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_coarsen_solution", this, &Heat::task_coarsen_solution );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), Ghost::None, 0 );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), nullptr, Task::FineLevel, nullptr, Task::NormalDomain, Ghost::None, 0 );
    task->requires ( Task::NewDW, u_label, nullptr, Task::FineLevel, nullptr, Task::NormalDomain, Ghost::None, 0 );
    task->modifies ( u_label );
    sched->addTask ( task, level_coarse->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleErrorEstimate
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleErrorEstimate on level " << level->getIndex() << " " );

    scheduleErrorEstimate_solution<AMR> ( level, sched );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleErrorEstimate_solution (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleErrorEstimate_solution on level " << level->getIndex() << " " );

    Task * task = scinew Task ( "Heat::task_error_estimate_solution", this, &Heat::task_error_estimate_solution );
    task->requires ( Task::NewDW, this->getSubProblemsLabel(), FGT, FGN );
    task->requires ( Task::NewDW, u_label, FGT, FGN );
    task->modifies ( this->m_regridder->getRefineFlagLabel(), this->m_regridder->refineFlagMaterials() );
    sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::scheduleErrorEstimate_solution (
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleErrorEstimate_solution on level " << level->getIndex() << " " );

    if ( !level->hasCoarserLevel() ) scheduleErrorEstimate_solution < !MG > ( level, sched );
    else
    {
        Task * task = scinew Task ( "Heat::task_error_estimate_solution", this, &Heat::task_error_estimate_solution );
        task->requires ( Task::NewDW, this->getSubProblemsLabel(), FGT, FGN );
        task->requires ( Task::NewDW, this->getSubProblemsLabel(), nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
        task->requires ( Task::NewDW, u_label, FGT, FGN );
        task->requires ( Task::NewDW, u_label, nullptr, Task::CoarseLevel, nullptr, Task::NormalDomain, CGT, CGN );
        task->modifies ( this->m_regridder->getRefineFlagLabel(), this->m_regridder->refineFlagMaterials() );
        sched->addTask ( task, level->eachPatch(), this->m_materialManager->allMaterials() );
    }
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::scheduleInitialErrorEstimate
(
    const LevelP & level,
    SchedulerP & sched
)
{
    DOUTR ( dbg_heat_scheduling, "scheduleInitialErrorEstimate on level " << level->getIndex() << " " );

    scheduleErrorEstimate ( level, sched );
}

// TASKS

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_initialize_solution (
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset *,
    DataWarehouse *,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();
    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_initialize_solution ====" );

    BBox box;
    getLevel ( patches )->getGrid()->getSpatialRange ( box );
    Vector L = box.max() - box.min();
    if ( L != box.max().asVector() ) L /= 2;

    ASSERTMSG ( DIM < D2 || L[Y] == L[X], "grid geometry must be a square" );
    ASSERTMSG ( DIM < D3 || L[Z] == L[X], "grid geometry must be a cube" );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        BlockRange range ( this->get_range ( patch ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over range " << range );

        DWView < ScalarField<double>, VAR, DIM > u ( dw_new, u_label, material, patch );
        parallel_for ( range, [patch, &L, &u, this] ( int i, int j, int k )->void { initialize_solution ( {i, j, k}, patch, L[X], u ); } );
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_restart_initialize_hypresstruct (
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * /*matls*/,
    DataWarehouse * /*dw_old*/,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();
    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_restart_initialize_hypresstruct ====" );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        _AdditionalEntries additional_entries;
        additional_entries.setData ( scinew HypreSStruct::AdditionalEntries );
        dw_new->put ( additional_entries, additional_entries_label, material, patch );
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_compute_stable_timestep (
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset *,
    DataWarehouse * /*dw_old*/,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();
    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_compute_stable_timestep ====" );

    if ( TST && dw_new && dw_new->exists ( u_norm2_L2_label ) )
    {
        sum_vartype u_norm2;
        dw_new->get ( u_norm2, u_norm2_L2_label );

        BBox box;
        getLevel ( patches )->getGrid()->getSpatialRange ( box );
        Vector L = box.max() - box.min();
        if ( L != box.max().asVector() ) L /= 2;

        double u0_norm2 = 1.;
        for ( size_t d = 0; d < DIM; ++d )
            u0_norm2 *= L[d];

        if ( u_norm2 > 2 * u0_norm2 )
            SCI_THROW ( AssertionFailed ( "\n ERROR: Unstable simulation\n", __FILE__, __LINE__ ) );
    }

    dw_new->put ( delt_vartype ( delt ), this->getDelTLabel(), getLevel ( patches ) );
    DOUT ( this->m_dbg_lvl2, myrank );
}

#ifdef PhaseField_Heat_DBG_DERIVATIVES
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_dbg_derivatives (
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset *,
    DataWarehouse * /*dw_old*/,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_dbg_derivatives ====" );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < VectorField<double, DIM>, VAR, DIM > du ( dw_new, du_label, material, patch );
        DWView < VectorField<double, DIM>, VAR, DIM > ddu ( dw_new, ddu_label, material, patch );

        SubProblems < HeatProblem<VAR, STN, TST> > subproblems ( dw_new, this->getSubProblemsLabel(), material, patch );

        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );
            FDView < ScalarField<const double>, STN > & u = p.template get_fd_view<U> ( dw_new );
            parallel_for ( p.get_range(), [&u, &du, &ddu, this] ( int i, int j, int k )->void { time_advance_dbg_derivatives ( {i, j, k}, u, du, ddu ); } );
        }
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG, bool T >
typename std::enable_if < T && !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_dbg_derivatives_error
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * /*matls*/,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_dbg_derivatives_error ====" );

    std::array<double, 4> norms {{ 0., 0., 0., 0. }}; // { u_norm2_H10, u_norm2_H20, error_norm2_H10, error_norm2_H20 }

    simTime_vartype simTimeVar;
    double simTime;
    if ( dw_old )
    {
        dw_old->get ( simTimeVar, VarLabel::find ( simTime_name ) );
        simTime = simTimeVar + delt;
    }
    else
    {
        dw_new->get ( simTimeVar, VarLabel::find ( simTime_name ) );
        simTime = simTimeVar;
    }

    const Level * level = getLevel ( patches );
    Grid * grid = level->getGrid().get_rep();
    const Vector h ( level->dCell() );

    BBox box;
    grid->getSpatialRange ( box );
    Vector L = box.max() - box.min();
    if ( L != box.max().asVector() ) L /= 2;

    ASSERTMSG ( DIM < D2 || L[Y] == L[X], "grid geometry must be a square" );
    ASSERTMSG ( DIM < D3 || L[Z] == L[X], "grid geometry must be a cube" );

    double a = M_PI_2 / L[X];
    double e = a * a * alpha * static_cast<double> ( DIM );
    double ut = exp ( -e * simTime );

    double area = 1.;
    for ( size_t D = 0; D < DIM; ++D )
        area *= h[D];

    // Since NC grid is 3D we have double the nodes
    // in each direction above DIM
    double factor = 1.;
    if ( VAR == NC )
        for ( size_t D = DIM; D < 3; ++D )
            factor *= .5;

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        IntVector low = this->template get_low<CC> ( patch );
        IntVector high = this->template get_high<CC> ( patch ) + ( VAR == CC ? 0 : 1 );

        DWView < VectorField<const double, DIM>, VAR, DIM > du ( du_label, material );
        DWView < VectorField<const double, DIM>, VAR, DIM > ddu ( ddu_label, material );
        du.set ( dw_new, patch->getLevel(), low, high, false );
        ddu.set ( dw_new, patch->getLevel(), low, high, false );

        DWView < VectorField<double, DIM>, VAR, DIM > epsilon_du ( dw_new, epsilon_du_label, material, patch );
        DWView < VectorField<double, DIM>, VAR, DIM > epsilon_ddu ( dw_new, epsilon_ddu_label, material, patch );

        DWView < VectorField<double, DIM>, CC, DIM > error_du ( dw_new, error_du_label, material, patch );
        DWView < VectorField<double, DIM>, CC, DIM > error_ddu ( dw_new, error_ddu_label, material, patch );

        BlockRange range ( this->get_range ( patch ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over range " << range );
        parallel_for ( range, [patch, &ut, &a, &du, &ddu, &epsilon_du, &epsilon_ddu, this] ( int i, int j, int k )->void { time_advance_dbg_derivatives_epsilon<MG> ( {i, j, k}, patch, ut, a, du, ddu, epsilon_du, epsilon_ddu ); } );

        BlockRange range_cc ( this->template get_range<CC> ( patch ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over CC range " << range_cc );
        parallel_reduce_sum ( range_cc, [patch, &ut, &a, &area, &factor, &du, &ddu, &error_du, &error_ddu, this] ( int i, int j, int k, std::array<double, 4> & norms )->void { time_advance_dbg_derivatives_error<MG> ( {i, j, k}, patch, ut, a, area, factor, du, ddu, error_du, error_ddu, norms[0], norms[1], norms[2], norms[3] ); }, norms );
    }

    dw_new->put ( sum_vartype ( norms[0] ), u_norm2_H10_label );
    dw_new->put ( sum_vartype ( norms[1] ), u_norm2_H20_label );
    dw_new->put ( sum_vartype ( norms[2] ), error_norm2_H10_label );
    dw_new->put ( sum_vartype ( norms[3] ), error_norm2_H20_label );

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG, bool T >
typename std::enable_if < T && MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_dbg_derivatives_error
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * /*matls*/,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_dbg_derivatives_error ====" );

    std::array<double, 4> norms {{ 0., 0., 0., 0. }}; // { u_norm2_H10, u_norm2_H20, error_norm2_H10, error_norm2_H20 }

    simTime_vartype simTimeVar;
    double simTime;
    if ( dw_old )
    {
        dw_old->get ( simTimeVar, VarLabel::find ( simTime_name ) );
        simTime = simTimeVar + delt;
    }
    else
    {
        dw_new->get ( simTimeVar, VarLabel::find ( simTime_name ) );
        simTime = simTimeVar;
    }

    const Level * level = getLevel ( patches );
    Grid * grid = level->getGrid().get_rep();
    const Vector h ( level->dCell() );

    int index = level->getIndex();
    int k = this->m_regridder->maxLevels() - index - 1;

    ReferenceGrid<DIM> grid_finest ( grid, index );
    grid_finest.addFinestLevel ( k );
    grid_finest.addReference();

    IntVector refinement = grid_finest.getLevel ( index + 1 )->getRefinementRatio();

    BBox box;
    grid->getSpatialRange ( box );
    Vector L = box.max() - box.min();
    if ( L != box.max().asVector() ) L /= 2;

    ASSERTMSG ( DIM < D2 || L[Y] == L[X], "grid geometry must be a square" );
    ASSERTMSG ( DIM < D3 || L[Z] == L[X], "grid geometry must be a cube" );

    double a = M_PI_2 / L[X];
    double e = a * a * alpha * static_cast<double> ( DIM );
    double ut = exp ( -e * simTime );

    double area = 1.;
    for ( size_t D = 0; D < DIM; ++D )
        area *= h[D] / refinement[D];

    // Since NC grid is 3D we have double the nodes
    // in each direction above DIM
    double factor = 1.;
    if ( VAR == NC )
        for ( size_t D = DIM; D < 3; ++D )
            factor *= .5;

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        Patch * patch_finest = grid_finest.addFinestPatch ( patch, index );

        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " ( ReferenceGrid Patch: " << *patch_finest << " Refinement: " << refinement << " ) " );

        DWView < VectorField<const double, DIM>, VAR, DIM > du ( dw_new, du_label, material, patch );
        DWView < VectorField<const double, DIM>, VAR, DIM > ddu ( dw_new, ddu_label, material, patch );

        AMRInterpolator < HeatProblem<VAR, STN, TST>, DU, C2F > du_finest ( dw_new, du_label, this->getSubProblemsLabel(), material, patch_finest );
        AMRInterpolator < HeatProblem<VAR, STN, TST>, DDU, C2F > ddu_finest ( dw_new, ddu_label, this->getSubProblemsLabel(), material, patch_finest );

        DWView < VectorField<double, DIM>, VAR, DIM > epsilon_du ( dw_new, epsilon_du_label, material, patch );
        DWView < VectorField<double, DIM>, VAR, DIM > epsilon_ddu ( dw_new, epsilon_ddu_label, material, patch );

        DWView < VectorField<double, DIM>, CC, DIM > error_du ( dw_new, error_du_label, material, patch );
        DWView < VectorField<double, DIM>, CC, DIM > error_ddu ( dw_new, error_ddu_label, material, patch );

        BlockRange range ( this->get_range ( patch ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over range " << range );
        parallel_for ( range, [patch, &simTime, &L, &du, &ddu, &epsilon_du, &epsilon_ddu, this] ( int i, int j, int k )->void { time_advance_dbg_derivatives_epsilon<MG> ( {i, j, k}, patch, simTime, L[0], du, ddu, epsilon_du, epsilon_ddu ); } );

        BlockRange range_cc ( this->template get_range<CC> ( patch ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over CC range " << range_cc );
        parallel_reduce_sum ( range_cc, [patch, patch_finest, &refinement, &ut, &a, &area, &factor, &du_finest, &ddu_finest, &error_du, &error_ddu, this] ( int i, int j, int k, std::array<double, 4> & norms )->void { time_advance_dbg_derivatives_error<MG> ( {i, j, k}, patch, patch_finest, refinement, ut, a, area, factor, du_finest, ddu_finest, error_du, error_ddu, norms[0], norms[1], norms[2], norms[3] ); }, norms );
    }

    grid_finest.removeReference();

    dw_new->put ( sum_vartype ( norms[0] ), u_norm2_H10_label );
    dw_new->put ( sum_vartype ( norms[1] ), u_norm2_H20_label );
    dw_new->put ( sum_vartype ( norms[2] ), error_norm2_H10_label );
    dw_new->put ( sum_vartype ( norms[3] ), error_norm2_H20_label );

    DOUT ( this->m_dbg_lvl2, myrank );
}
#endif

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_forward_euler (
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset *,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_forward_euler ====" );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < ScalarField<double>, VAR, DIM > u_new ( dw_new, u_label, material, patch );

        SubProblems < HeatProblem<VAR, STN, TST> > subproblems ( dw_new, this->getSubProblemsLabel(), material, patch );
        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );

            FDView < ScalarField<const double>, STN > & u_old = p.template get_fd_view<U> ( dw_old );
            parallel_for ( p.get_range(), [&u_old, &u_new, this] ( int i, int j, int k )->void { time_advance_solution_forward_euler ( {i, j, k}, u_old, u_new ); } );
        }
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

#ifdef HAVE_HYPRE
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_backward_euler_assemble_hypre
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * matls,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    timeStep_vartype timeStepVar;
    dw_old->get ( timeStepVar, VarLabel::find ( timeStep_name ) );
    double timeStep = timeStepVar;

    if ( timeStep == 1 || this->isRegridTimeStep() )
        task_time_advance_solution_backward_euler_assemble_hypre_full ( myworld, patches, matls, dw_old, dw_new );
    else
        task_time_advance_solution_backward_euler_assemble_hypre_rhs ( myworld, patches, matls, dw_old, dw_new );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_backward_euler_assemble_hypre_full
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset *,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_backward_euler_assemble_hypre_full ====" );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < ScalarField<Stencil7>, VAR, DIM > A ( dw_new, matrix_label, material, patch );
        DWView < ScalarField<double>, VAR, DIM > b ( dw_new, rhs_label, material, patch );
        SubProblems < HeatProblem<VAR, STN, TST> > subproblems ( dw_old, this->getSubProblemsLabel(), material, patch );

        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );

            FDView < ScalarField<const double>, STN > & u_old = p.template get_fd_view<U> ( dw_old );
            parallel_for ( p.get_range(), [&u_old, &A, &b, this] ( int i, int j, int k )->void { time_advance_solution_backward_euler_assemble_hypre_full ( {i, j, k}, u_old, A, b ); } );
        }
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_backward_euler_assemble_hypre_rhs
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * matls,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_backward_euler_assemble_hypre_rhs ====" );

    dw_new->transferFrom ( dw_old, matrix_label, patches, matls );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < ScalarField<double>, VAR, DIM > b ( dw_new, rhs_label, material, patch );
        SubProblems < HeatProblem<VAR, STN, TST> > subproblems ( dw_old, this->getSubProblemsLabel(), material, patch );

        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );

            FDView < ScalarField<const double>, STN > & u_old = p.template get_fd_view<U> ( dw_old );
            parallel_for ( p.get_range(), [&u_old, &b, this] ( int i, int j, int k )->void { time_advance_solution_backward_euler_assemble_hypre_rhs ( {i, j, k}, u_old, b ); } );
        }
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_backward_euler_assemble_hypresstruct
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * matls,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    timeStep_vartype timeStepVar;
    dw_old->get ( timeStepVar, VarLabel::find ( timeStep_name ) );
    double timeStep = timeStepVar;

    if ( timeStep == 1 || this->isRegridTimeStep() )
        task_time_advance_solution_backward_euler_assemble_hypresstruct_full ( myworld, patches, matls, dw_old, dw_new );
    else
        task_time_advance_solution_backward_euler_assemble_hypresstruct_rhs ( myworld, patches, matls, dw_old, dw_new );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_backward_euler_assemble_hypresstruct_full
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * /*matls*/,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_backward_euler_assemble_hypresstruct_full ====" );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < ScalarField<Stencil7>, VAR, DIM > A_stencil ( dw_new, matrix_label, material, patch );
        HypreSStruct::AdditionalEntries * A_additional = scinew HypreSStruct::AdditionalEntries;
        DWView < ScalarField<double>, VAR, DIM > b ( dw_new, rhs_label, material, patch );
        SubProblems < HeatProblem<VAR, STN, TST> > subproblems ( dw_old, this->getSubProblemsLabel(), material, patch );

        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );

            FDView < ScalarField<const double>, STN > & u_old = p.template get_fd_view<U> ( dw_old );
            parallel_for ( p.get_range(), [&u_old, &A_stencil, &A_additional, &b, this] ( int i, int j, int k )->void { time_advance_solution_backward_euler_assemble_hypresstruct_full ( {i, j, k}, u_old, A_stencil, A_additional, b ); } );
        }

        _AdditionalEntries additional_entries;
        additional_entries.setData ( A_additional );
        dw_new->put ( additional_entries, additional_entries_label, material, patch );
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_backward_euler_assemble_hypresstruct_rhs
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * matls,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_backward_euler_assemble_hypresstruct_rhs ====" );

    dw_new->transferFrom ( dw_old, matrix_label, patches, matls );
    dw_new->transferFrom ( dw_old, additional_entries_label, patches, matls );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < ScalarField<double>, VAR, DIM > b ( dw_new, rhs_label, material, patch );
        SubProblems < HeatProblem<VAR, STN, TST> > subproblems ( dw_old, this->getSubProblemsLabel(), material, patch );

        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );

            FDView < ScalarField<const double>, STN > & u_old = p.template get_fd_view<U> ( dw_old );
            parallel_for ( p.get_range(), [&u_old, &b, this] ( int i, int j, int k )->void { time_advance_solution_backward_euler_assemble_hypresstruct_rhs ( {i, j, k}, u_old, b ); } );
        }
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_crank_nicolson_assemble_hypre
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * matls,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    timeStep_vartype timeStepVar;
    dw_old->get ( timeStepVar, VarLabel::find ( timeStep_name ) );
    double timeStep = timeStepVar;

    if ( timeStep == 1 || this->isRegridTimeStep() )
        task_time_advance_solution_crank_nicolson_assemble_hypre_full ( myworld, patches, matls, dw_old, dw_new );
    else
        task_time_advance_solution_crank_nicolson_assemble_hypre_rhs ( myworld, patches, matls, dw_old, dw_new );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_crank_nicolson_assemble_hypre_full
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset *,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_crank_nicolson_assemble_hypre_full ====" );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < ScalarField<Stencil7>, VAR, DIM > A ( dw_new, matrix_label, material, patch );
        DWView < ScalarField<double>, VAR, DIM > b ( dw_new, rhs_label, material, patch );
        SubProblems < HeatProblem<VAR, STN, TST> > subproblems ( dw_old, this->getSubProblemsLabel(), material, patch );

        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );

            FDView < ScalarField<const double>, STN > & u_old = p.template get_fd_view<U> ( dw_old );
            parallel_for ( p.get_range(), [&u_old, &A, &b, this] ( int i, int j, int k )->void { time_advance_solution_crank_nicolson_assemble_hypre_full ( {i, j, k}, u_old, A, b ); } );
        }
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_crank_nicolson_assemble_hypre_rhs
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * matls,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_crank_nicolson_assemble_hypre_rhs ====" );

    dw_new->transferFrom ( dw_old, matrix_label, patches, matls );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < ScalarField<double>, VAR, DIM > b ( dw_new, rhs_label, material, patch );
        SubProblems < HeatProblem<VAR, STN, TST> > subproblems ( dw_old, this->getSubProblemsLabel(), material, patch );

        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );

            FDView < ScalarField<const double>, STN > & u_old = p.template get_fd_view<U> ( dw_old );
            parallel_for ( p.get_range(), [&u_old, &b, this] ( int i, int j, int k )->void { time_advance_solution_crank_nicolson_assemble_hypre_rhs ( {i, j, k}, u_old, b ); } );
        }
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_crank_nicolson_assemble_hypresstruct
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * matls,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    timeStep_vartype timeStepVar;
    dw_old->get ( timeStepVar, VarLabel::find ( timeStep_name ) );
    double timeStep = timeStepVar;

    if ( timeStep == 1 || this->isRegridTimeStep() )
        task_time_advance_solution_crank_nicolson_assemble_hypresstruct_full ( myworld, patches, matls, dw_old, dw_new );
    else
        task_time_advance_solution_crank_nicolson_assemble_hypresstruct_rhs ( myworld, patches, matls, dw_old, dw_new );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_crank_nicolson_assemble_hypresstruct_full
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * /*matls*/,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_crank_nicolson_assemble_hypresstruct_full ====" );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < ScalarField<Stencil7>, VAR, DIM > A_stencil ( dw_new, matrix_label, material, patch );
        HypreSStruct::AdditionalEntries * A_additional = scinew HypreSStruct::AdditionalEntries;
        DWView < ScalarField<double>, VAR, DIM > b ( dw_new, rhs_label, material, patch );
        SubProblems < HeatProblem<VAR, STN, TST> > subproblems ( dw_old, this->getSubProblemsLabel(), material, patch );

        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );

            FDView < ScalarField<const double>, STN > & u_old = p.template get_fd_view<U> ( dw_old );
            parallel_for ( p.get_range(), [&u_old, &A_stencil, &A_additional, &b, this] ( int i, int j, int k )->void { time_advance_solution_crank_nicolson_assemble_hypresstruct_full ( {i, j, k}, u_old, A_stencil, A_additional, b ); } );
        }

        _AdditionalEntries additional_entries;
        additional_entries.setData ( A_additional );
        dw_new->put ( additional_entries, additional_entries_label, material, patch );
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_crank_nicolson_assemble_hypresstruct_rhs
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * matls,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_crank_nicolson_assemble_hypresstruct_rhs ====" );

    dw_new->transferFrom ( dw_old, matrix_label, patches, matls );
    dw_new->transferFrom ( dw_old, additional_entries_label, patches, matls );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < ScalarField<double>, VAR, DIM > b ( dw_new, rhs_label, material, patch );
        SubProblems < HeatProblem<VAR, STN, TST> > subproblems ( dw_old, this->getSubProblemsLabel(), material, patch );

        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );

            FDView < ScalarField<const double>, STN > & u_old = p.template get_fd_view<U> ( dw_old );
            parallel_for ( p.get_range(), [&u_old, &b, this] ( int i, int j, int k )->void { time_advance_solution_crank_nicolson_assemble_hypresstruct_rhs ( {i, j, k}, u_old, b ); } );
        }
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

#   ifdef PhaseField_Heat_DBG_MATRIX
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_update_dbg_matrix
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * /*matls*/,
    DataWarehouse * /*dw_old*/,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_update_dbg_matrix ====" );

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        DWView < ScalarField<const Stencil7>, VAR, DIM > A ( dw_new, matrix_label, material, patch );

        DWView < ScalarField<double>, VAR, DIM > Ap ( dw_new, Ap_label, material, patch );
        DWView < ScalarField<double>, VAR, DIM > Aw ( dw_new, Aw_label, material, patch );
        DWView < ScalarField<double>, VAR, DIM > Ae ( dw_new, Ae_label, material, patch );
        DWView < ScalarField<double>, VAR, DIM > As ( dw_new, As_label, material, patch );
        DWView < ScalarField<double>, VAR, DIM > An ( dw_new, An_label, material, patch );
        DWView < ScalarField<double>, VAR, DIM > Ab ( dw_new, Ab_label, material, patch );
        DWView < ScalarField<double>, VAR, DIM > At ( dw_new, At_label, material, patch );

        BlockRange range ( this->get_range ( patch ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over range " << range );
        parallel_for ( range, [&A, &Ap, &Aw, &Ae, &As, &An, &Ab, &At, this] ( int i, int j, int k )->void { time_advance_update_dbg_matrix ( {i, j, k}, A, Ap, Aw, Ae, As, An, Ab, At ); } );
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}
#   endif
#endif // HAVE_HYPRE

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG, bool T >
typename std::enable_if < T && !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_error
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * /*matls*/,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_error ====" );

    std::array<double, 2> norms {{ 0., 0. }}; // { u_norm2_discrete, error_norm2_discrete, u_norm2_L2, error_norm2_L2 }

    simTime_vartype simTimeVar;
    double simTime;
    if ( dw_old )
    {
        dw_old->get ( simTimeVar, VarLabel::find ( simTime_name ) );
        simTime = simTimeVar + delt;
    }
    else
    {
        dw_new->get ( simTimeVar, VarLabel::find ( simTime_name ) );
        simTime = simTimeVar;
    }

    const Level * level = getLevel ( patches );
    Grid * grid = level->getGrid().get_rep();
    const Vector h ( level->dCell() );

    BBox box;
    grid->getSpatialRange ( box );
    Vector L = box.max() - box.min();
    if ( L != box.max().asVector() ) L /= 2;

    ASSERTMSG ( DIM < D2 || L[Y] == L[X], "grid geometry must be a square" );
    ASSERTMSG ( DIM < D3 || L[Z] == L[X], "grid geometry must be a cube" );

    double a = M_PI_2 / L[X];
    double e = a * a * alpha * static_cast<double> ( DIM );
    double ut = exp ( -e * simTime );

    double area = 1.;
    for ( size_t D = 0; D < DIM; ++D )
        area *= h[D];

    // Since NC grid is 3D we have double the nodes
    // in each direction above DIM
    double factor = 1.;
    if ( VAR == NC )
        for ( size_t D = DIM; D < 3; ++D )
            factor *= .5;

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        IntVector low = this->template get_low<CC> ( patch );
        IntVector high = this->template get_high<CC> ( patch ) + ( VAR == CC ? 0 : 1 );

        DWView < ScalarField<const double>, VAR, DIM > u ( u_label, material );
        u.set ( dw_new, patch->getLevel(), low, high, false );

        DWView < ScalarField<double>, VAR, DIM > epsilon_u ( dw_new, epsilon_u_label, material, patch );
        DWView < ScalarField<double>, CC, DIM > error_u ( dw_new, error_u_label, material, patch );

        BlockRange range ( this->get_range ( patch ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over range " << range );
        parallel_for ( range, [patch, &ut, &a, &u, &epsilon_u, this] ( int i, int j, int k )->void { time_advance_solution_epsilon<MG> ( {i, j, k}, patch, ut, a, u, epsilon_u ); } );

        BlockRange range_cc ( this->template get_range<CC> ( patch ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over CC range " << range_cc );
        parallel_reduce_sum ( range_cc, [patch, &ut, &a, &area, &factor, &u, &error_u, this] ( int i, int j, int k, std::array<double, 2> & norms )->void { time_advance_solution_error<MG> ( {i, j, k}, patch, ut, a, area, factor, u, error_u, norms[0], norms[1] ); }, norms );
    }

    dw_new->put ( sum_vartype ( norms[0] ), u_norm2_L2_label );
    dw_new->put ( sum_vartype ( norms[1] ), error_norm2_L2_label );

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG, bool T >
typename std::enable_if < T && MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::task_time_advance_solution_error
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * /*matls*/,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_time_advance_solution_error ====" );

    std::array<double, 2> norms {{ 0., 0. }}; // { u_norm2_L2, error_norm2_L2 }

    simTime_vartype simTimeVar;
    double simTime;
    if ( dw_old )
    {
        dw_old->get ( simTimeVar, VarLabel::find ( simTime_name ) );
        simTime = simTimeVar + delt;
    }
    else
    {
        dw_new->get ( simTimeVar, VarLabel::find ( simTime_name ) );
        simTime = simTimeVar;
    }

    const Level * level = getLevel ( patches );
    Grid * grid = level->getGrid().get_rep();
    const Vector h ( level->dCell() );

    int index = level->getIndex();
    int k = this->m_regridder->maxLevels() - index - 1;

    ReferenceGrid<DIM> grid_finest ( grid, index );
    grid_finest.addFinestLevel ( k );
    grid_finest.addReference();

    IntVector refinement = grid_finest.getLevel ( index + 1 )->getRefinementRatio();

    BBox box;
    grid->getSpatialRange ( box );
    Vector L = box.max() - box.min();
    if ( L != box.max().asVector() ) L /= 2;

    ASSERTMSG ( DIM < D2 || L[Y] == L[X], "grid geometry must be a square" );
    ASSERTMSG ( DIM < D3 || L[Z] == L[X], "grid geometry must be a cube" );

    double a = M_PI_2 / L[X];
    double e = a * a * alpha * static_cast<double> ( DIM );
    double ut = exp ( -e * simTime );

    double area = 1.;
    for ( size_t D = 0; D < DIM; ++D )
        area *= h[D] / refinement[D];

    // Since NC grid is 3D we have double the nodes
    // in each direction above DIM
    double factor = 1.;
    if ( VAR == NC )
        for ( size_t D = DIM; D < 3; ++D )
            factor *= .5;

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        Patch * patch_finest = grid_finest.addFinestPatch ( patch, index );

        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " ( ReferenceGrid Patch: " << *patch_finest << " Refinement: " << refinement << " ) " );

        DWView < ScalarField<const double>, VAR, DIM > u ( dw_new, u_label, material, patch );
        AMRInterpolator < HeatProblem<VAR, STN, TST>, U, C2F > u_finest ( dw_new, u_label, this->getSubProblemsLabel(), material, patch_finest );

        DWView < ScalarField<double>, VAR, DIM > epsilon_u ( dw_new, epsilon_u_label, material, patch );
        DWView < ScalarField<double>, CC, DIM > error_u ( dw_new, error_u_label, material, patch );

        BlockRange range ( this->get_range ( patch ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over range " << range );
        parallel_for ( range, [patch, &ut, &a, &u, &epsilon_u, this] ( int i, int j, int k )->void { time_advance_solution_epsilon<MG> ( {i, j, k}, patch, ut, a, u, epsilon_u ); } );

        BlockRange range_cc ( this->template get_range<CC> ( patch ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over CC range " << range );
        parallel_reduce_sum ( range_cc, [patch, patch_finest, &refinement, &ut, &a, &area, &factor, &u_finest, &error_u, this] ( int i, int j, int k, std::array<double, 2> & norms )->void { time_advance_solution_error<MG> ( {i, j, k}, patch, patch_finest, refinement, ut, a, area, factor, u_finest, error_u, norms[0], norms[1] ); }, norms );
    }

    grid_finest.removeReference();

    dw_new->put ( sum_vartype ( norms[0] ), u_norm2_L2_label );
    dw_new->put ( sum_vartype ( norms[1] ), error_norm2_L2_label );

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_refine_solution
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches_fine,
    const MaterialSubset * /*matls*/,
    DataWarehouse * /*dw_old*/,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_refine_solution ====" );

    for ( int p = 0; p < patches_fine->size(); ++p )
    {
        const Patch * patch_fine = patches_fine->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Fine Patch: " << *patch_fine << " Level: " << patch_fine->getLevel()->getIndex() << " " );

        DWView < ScalarField<double>, VAR, DIM > u_fine ( dw_new, u_label, material, patch_fine );

        AMRInterpolator < HeatProblem<VAR, STN, TST>, U, C2F > u_coarse_interp ( dw_new, u_label, this->getSubProblemsLabel(), material, patch_fine );

        BlockRange range_fine ( this->get_range ( patch_fine ) );
        DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over fine range" << range_fine );
        parallel_for ( range_fine, [&u_coarse_interp, &u_fine, this] ( int i, int j, int k )->void { refine_solution ( {i, j, k}, u_coarse_interp, u_fine ); } );
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_coarsen_solution (
    const ProcessorGroup * myworld,
    const PatchSubset * patches_coarse,
    const MaterialSubset * /*matls*/,
    DataWarehouse * /*dw_old*/,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_coarsen_solution " );

    for ( int p = 0; p < patches_coarse->size(); ++p )
    {
        const Patch * patch_coarse = patches_coarse->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Coarse Patch: " << *patch_coarse << " Level: " << patch_coarse->getLevel()->getIndex() << " " );

        DWView < ScalarField<double>, VAR, DIM > u_coarse ( dw_new, u_label, material, patch_coarse );

        AMRRestrictor < HeatProblem<VAR, STN, TST>, U, F2C > u_fine_restr ( dw_new, u_label, this->getSubProblemsLabel(), material, patch_coarse, false );

        for ( const auto & region : u_fine_restr.get_support() )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over coarse cells region " << region );
            BlockRange range_coarse (
                Max ( region.getLow(), this->get_low ( patch_coarse ) ),
                Min ( region.getHigh(), this->get_high ( patch_coarse ) )
            );

            parallel_for ( range_coarse, [&u_fine_restr, &u_coarse, this] ( int i, int j, int k )->void { coarsen_solution ( {i, j, k}, u_fine_restr, u_coarse ); } );
        }
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::task_error_estimate_solution
(
    const ProcessorGroup * myworld,
    const PatchSubset * patches,
    const MaterialSubset * /*matls*/,
    DataWarehouse * dw_old,
    DataWarehouse * dw_new
)
{
    int myrank = myworld->myRank();

    DOUT ( this->m_dbg_lvl1, myrank << "==== Heat::task_error_estimate_solution " );

    BBox box;
    const Level * level = getLevel ( patches );
    Grid * grid = level->getGrid().get_rep();
    grid->getSpatialRange ( box );
    Vector L = box.max() - box.min();
    if ( L != box.max().asVector() ) L /= 2;

    double a = M_PI_2 / L[X];

    for ( int p = 0; p < patches->size(); ++p )
    {
        const Patch * patch = patches->get ( p );
        DOUT ( this->m_dbg_lvl2, myrank << "== Patch: " << *patch << " Level: " << patch->getLevel()->getIndex() << " " );

        int k = patch->getLevel()->getIndex();
        double r2 = a*a*(1-std::pow(refine_threshold,k+1));

        DWView < ScalarField<int>, CC, DIM > refine_flag ( dw_new, this->m_regridder->getRefineFlagLabel(), material, patch );
        refine_flag.initialize ( 0 );

        SubProblems< HeatProblem<VAR, STN, TST> > subproblems ( dw_new, this->getSubProblemsLabel(), material, patch );
        for ( const auto & p : subproblems )
        {
            DOUT ( this->m_dbg_lvl3, myrank << "= Iterating over " << p );
            FDView < ScalarField<const double>, STN > & u = p.template get_fd_view<U> ( dw_new );
            parallel_for ( p.get_range(), [&r2, &u, &refine_flag, this] ( int i, int j, int k )->void { error_estimate_solution<VAR> ( {i, j, k}, r2, u, refine_flag ); } );
        }
    }

    DOUT ( this->m_dbg_lvl2, myrank );
}

// IMPLEMENTATIONS

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::initialize_solution (
    const IntVector & id,
    Patch const * patch,
    const double & L,
    View < ScalarField<double> > & u
)
{
    Vector v ( this->get_position ( patch, id ).asVector() );
    double a = M_PI_2 / L;
    u[id] = 1.;
    for ( size_t d = 0; d < DIM; ++d )
        u[id] *= cos ( a * v[d] );
}

#ifdef PhaseField_Heat_DBG_DERIVATIVES
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::time_advance_dbg_derivatives (
    const IntVector & id,
    const FDView < ScalarField<const double>, STN > & u,
    View < VectorField<double, DIM> > & du,
    View < VectorField<double, DIM> > & ddu
)
{
    du[X][id] = u.dx ( id );
    ddu[X][id] = u.dxx ( id );
    if ( DIM > D1 )
    {
        du[Y][id] = u.dy ( id );
        ddu[Y][id] = u.dyy ( id );
    }
    if ( DIM > D2 )
    {
        du[Z][id] = u.dz ( id );
        ddu[Z][id] = u.dzz ( id );
    }
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::time_advance_dbg_derivatives_epsilon (
    const IntVector & id,
    const Patch * patch,
    const double & ut,
    const double & a,
    const View < VectorField<const double, DIM> > & du,
    const View < VectorField<const double, DIM> > & ddu,
    View < VectorField<double, DIM> > & epsilon_du,
    View < VectorField<double, DIM> > & epsilon_ddu
)
{
    Vector v ( this->get_position ( patch, id ).asVector() );
    double ddu_ex = -a * ut;
    double du_ex[] = { ddu_ex, ddu_ex, ddu_ex };
    ddu_ex *= a;

    for ( size_t i = 0; i < DIM; ++i )
    {
        double c = cos ( a * v[i] );
        double s = sin ( a * v[i] );
        du_ex[i] *= s;
        ddu_ex *= c;
        for ( size_t j = 0; j < i; ++j )
            du_ex[j] *= c;
        for ( size_t j = i + 1; j < DIM; ++j )
            du_ex[j] *= c;
    }

    for ( size_t i = 0; i < DIM; ++i )
    {
        epsilon_du[i][id] = du[i][id] - du_ex[i];
        epsilon_ddu[i][id] = ddu[i][id] - ddu_ex;
    }
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::time_advance_dbg_derivatives_epsilon (
    const IntVector & id,
    const Patch * patch,
    const double & ut,
    const double & a,
    const View < VectorField<const double, DIM> > & du,
    const View < VectorField<const double, DIM> > & ddu,
    View < VectorField<double, DIM> > & epsilon_du,
    View < VectorField<double, DIM> > & epsilon_ddu
)
{
    if ( this->is_refined ( patch->getLevel(), patch, id ) )
        for ( size_t i = 0; i < DIM; ++i )
        {
            epsilon_du[i][id] = 0.;
            epsilon_ddu[i][id] = 0.;
        }
    else
        time_advance_dbg_derivatives_epsilon < !MG > ( id, patch, ut, a, du, ddu, epsilon_du, epsilon_ddu );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::time_advance_dbg_derivatives_error (
    const IntVector & id,
    const Patch * patch,
    const double & ut,
    const double & a,
    const double & area,
    const double & factor,
    const View < VectorField<const double, DIM> > & du,
    const View < VectorField<const double, DIM> > & ddu,
    View < VectorField<double, DIM> > & error_du,
    View < VectorField<double, DIM> > & error_ddu,
    double & u_norm2_H10,
    double & u_norm2_H20,
    double & error_norm2_H10,
    double & error_norm2_H20
)
{
    double du2[] = { 0., 0., 0.}, lapu2 = 0.;
    double du_err2[] = { 0., 0., 0.}, ddu_err2[] = { 0., 0., 0.}, lapu_err2 = 0.;

    auto do_loop = [this, &patch, &ut, &a, &du, &ddu, &area, &du2, &du_err2, &ddu_err2, &lapu2, &lapu_err2 ] ( IntVector id, double w = 1. )
    {
        Vector v ( this->get_position ( patch, id ).asVector() );
        double ddu_ex = -a * ut;
        double du_ex[] = { ddu_ex, ddu_ex, ddu_ex };
        ddu_ex *= a;

        for ( size_t i = 0; i < DIM; ++i )
        {
            double c = cos ( a * v[i] );
            double s = sin ( a * v[i] );
            du_ex[i] *= s;
            ddu_ex *= c;
            for ( size_t j = 0; j < i; ++j )
                du_ex[j] *= c;
            for ( size_t j = i + 1; j < DIM; ++j )
                du_ex[j] *= c;
        }

        double duf[DIM], dduf[DIM];
        double edu[DIM], eddu[DIM];
        double lapuf = 0.;
        double elapu = 0.;
        for ( size_t i = 0; i < DIM; ++i )
        {
            duf[i] = du[i][id];
            dduf[i] = ddu[i][id];
            edu[i] = duf[i] - du_ex[i];
            eddu[i] = dduf[i] - ddu_ex;

#ifdef EXACT_NORM
            lapuf += ddu_ex;
#else
            lapuf += dduf[i];
#endif
            elapu += eddu[i];
#ifdef EXACT_NORM
            du2[i] += w * area * du_ex[i] * du_ex[i];
#else
            du2[i] += w * area * duf[i] * duf[i];
#endif
            du_err2[i] += w * area * edu[i] * edu[i];
            ddu_err2[i] += w * area * eddu[i] * eddu[i];
        }

        lapu2 += w * area * lapuf * lapuf;
        lapu_err2 += w * area * elapu * elapu;
    };

    if ( VAR == CC )
    {
        do_loop ( id );
    }
    else
    {
        IntVector offset;
        for ( offset[X] = 0; offset[X] < 2; ++offset[X] )
            for ( offset[Y] = 0; offset[Y] < 2; ++offset[Y] )
                for ( offset[Z] = 0; offset[Z] < 2; ++offset[Z] )
                    do_loop ( id + offset, 0.125 );
    }

    u_norm2_H20 += lapu2;
    error_norm2_H20 += lapu_err2;

    for ( size_t i = 0; i < DIM; ++i )
    {
        u_norm2_H10 += du2[i];
        error_norm2_H10 += du_err2[i];
        error_du[i][id] = sqrt ( du_err2[i] / ( factor * area ) );
        error_ddu[i][id] = sqrt ( ddu_err2[i] / ( factor * area ) );
    }
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::time_advance_dbg_derivatives_error (
    const IntVector & id,
    const Patch * patch,
    const Patch * patch_finest,
    const IntVector & refinement,
    const double & ut,
    const double & a,
    const double & area,
    const double & factor,
    const View < VectorField<const double, DIM> > & du_finest,
    const View < VectorField<const double, DIM> > & ddu_finest,
    View < VectorField<double, DIM> > & error_du,
    View < VectorField<double, DIM> > & error_ddu,
    double & u_norm2_H10,
    double & u_norm2_H20,
    double & error_norm2_H10,
    double & error_norm2_H20
)
{
    const Level * level ( patch->getLevel() );

    if ( this->is_refined ( level, patch, id ) )
    {
        for ( size_t i = 0; i < DIM; ++i )
        {
            error_du[i][id] = 0.;
            error_ddu[i][id] = 0.;
        }
        return;
    }

    double du2[] = { 0., 0., 0.}, lapu2 = 0.;
    double du_err2[] = { 0., 0., 0.}, ddu_err2[] = { 0., 0., 0.}, lapu_err2 = 0.;

    auto do_loop = [this, &patch_finest, &ut, &a, &du_finest, &ddu_finest, &area, &du2, &du_err2, &ddu_err2, &lapu2, &lapu_err2 ] ( IntVector idf, double w = 1. )
    {
        Vector v ( this->get_position ( patch_finest, idf ).asVector() );
        double ddu_ex = -a * ut;
        double du_ex[] = { ddu_ex, ddu_ex, ddu_ex };
        ddu_ex *= a;

        for ( size_t i = 0; i < DIM; ++i )
        {
            double c = cos ( a * v[i] );
            double s = sin ( a * v[i] );
            du_ex[i] *= s;
            ddu_ex *= c;
            for ( size_t j = 0; j < i; ++j )
                du_ex[j] *= c;
            for ( size_t j = i + 1; j < DIM; ++j )
                du_ex[j] *= c;
        }

        double duf[DIM], dduf[DIM];
        double edu[DIM], eddu[DIM];
        double lapuf = 0.;
        double elapu = 0.;
        for ( size_t i = 0; i < DIM; ++i )
        {
            duf[i] = du_finest[i][idf];
            dduf[i] = ddu_finest[i][idf];
            edu[i] = duf[i] - du_ex[i];
            eddu[i] = dduf[i] - ddu_ex;
#ifdef EXACT_NORM
            lapuf += ddu_ex;
#else
            lapuf += dduf[i];
#endif
            elapu += eddu[i];
#ifdef EXACT_NORM
            du2[i] += w * area * du_ex[i] * du_ex[i];
#else
            du2[i] += w * area * duf[i] * duf[i];
#endif
            du_err2[i] += w * area * edu[i] * edu[i];
            ddu_err2[i] += w * area * eddu[i] * eddu[i];
        }

        lapu2 += area * lapuf * lapuf;
        lapu_err2 += area * elapu * elapu;
    };

    IntVector id_finest = id * refinement;
    IntVector offset;

    if ( VAR == CC )
    {
        do_loop ( id_finest );
    }
    else
    {
        for ( offset[X] = 0; offset[X] < 2; ++offset[X] )
            for ( offset[Y] = 0; offset[Y] < 2; ++offset[Y] )
                for ( offset[Z] = 0; offset[Z] < 2; ++offset[Z] )
                    do_loop ( id_finest + offset, 0.125 );
    }

    u_norm2_H20 += lapu2;
    error_norm2_H20 += lapu_err2;

    for ( size_t i = 0; i < DIM; ++i )
    {
        u_norm2_H10 += du2[i];
        error_norm2_H10 += du_err2[i];
        error_du[i][id] = sqrt ( du_err2[i] / factor );
        error_ddu[i][id] = sqrt ( ddu_err2[i] / factor );
    }
}
#endif

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_forward_euler (
    const IntVector & id,
    const FDView < ScalarField<const double>, STN > & u_old,
    View < ScalarField<double> > & u_new
)
{
    double epsilon_u = delt * alpha * u_old.laplacian ( id );
    u_new[id] = u_old[id] + epsilon_u;
}

#ifdef HAVE_HYPRE
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_backward_euler_assemble_hypre_full
(
    const IntVector & id,
    const FDView < ScalarField<const double>, STN > & u_old,
    View < ScalarField<Stencil7> > & A,
    View < ScalarField<double> > & b
)
{
    std::tuple<Stencil7, double> sys = u_old.laplacian_sys_hypre ( id );

    const Stencil7 & lap_stn = std::get<0> ( sys );
    const double & rhs = std::get<1> ( sys );
    const double a = alpha * delt;

    for ( int i = 0; i < 7; ++i )
        A[id][i] = -a * lap_stn[i];
    A[id].p += 1;
    b[id] = u_old[id] + a * rhs;
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_backward_euler_assemble_hypre_rhs
(
    const IntVector & id,
    const FDView < ScalarField<const double>, STN > & u_old,
    View < ScalarField<double> > & b
)
{
    double rhs = u_old.laplacian_rhs_hypre ( id );
    const double a = alpha * delt;

    b[id] = u_old[id] + a * rhs;
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_backward_euler_assemble_hypresstruct_full
(
    const IntVector & id,
    const FDView < ScalarField<const double>, STN > & u_old,
    View < ScalarField<Stencil7> > & A_stencil,
    HypreSStruct::AdditionalEntries * A_additional,
    View < ScalarField<double> > & b
)
{
    std::tuple<Stencil7, HypreSStruct::AdditionalEntries, double> sys = u_old.laplacian_sys_hypresstruct ( id );

    const Stencil7 & lap_stn = std::get<0> ( sys );
    HypreSStruct::AdditionalEntries & lap_add = std::get<1> ( sys );
    const double & rhs = std::get<2> ( sys );
    const double a = alpha * delt;

    for ( int i = 0; i < 7; ++i )
        A_stencil[id][i] = -a * lap_stn[i];
    A_stencil[id].p += 1;
    for ( auto & entry : lap_add )
        *A_additional += -a * entry;
    b[id] = u_old[id] + a * rhs;
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_backward_euler_assemble_hypresstruct_rhs
(
    const IntVector & id,
    const FDView < ScalarField<const double>, STN > & u_old,
    View < ScalarField<double> > & b
)
{
    double rhs = u_old.laplacian_rhs_hypresstruct ( id );
    const double a = alpha * delt;

    b[id] = u_old[id] + a * rhs;
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_crank_nicolson_assemble_hypre_full
(
    const IntVector & id,
    const FDView < ScalarField<const double>, STN > & u_old,
    View < ScalarField<Stencil7> > & A,
    View < ScalarField<double> > & b
)
{
    std::tuple<Stencil7, double> sys = u_old.laplacian_sys_hypre ( id );

    const Stencil7 & lap_stn = std::get<0> ( sys );
    const double & rhs = std::get<1> ( sys );
    const double a = 0.5 * alpha * delt;

    for ( int i = 0; i < 7; ++i )
        A[id][i] = -a * lap_stn[i];
    A[id].p += 1;
    b[id] = u_old[id] + a * ( rhs + u_old.laplacian ( id ) );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_crank_nicolson_assemble_hypre_rhs
(
    const IntVector & id,
    const FDView < ScalarField<const double>, STN > & u_old,
    View < ScalarField<double> > & b
)
{
    double rhs = u_old.laplacian_rhs_hypre ( id );
    const double a = 0.5 * alpha * delt;

    b[id] = u_old[id] + a * ( rhs + u_old.laplacian ( id ) );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_crank_nicolson_assemble_hypresstruct_full
(
    const IntVector & id,
    const FDView < ScalarField<const double>, STN > & u_old,
    View < ScalarField<Stencil7> > & A_stencil,
    HypreSStruct::AdditionalEntries * A_additional,
    View < ScalarField<double> > & b
)
{
    std::tuple<Stencil7, HypreSStruct::AdditionalEntries, double> sys = u_old.laplacian_sys_hypresstruct ( id );

    const Stencil7 & lap_stn = std::get<0> ( sys );
    HypreSStruct::AdditionalEntries & lap_add = std::get<1> ( sys );
    const double & rhs = std::get<2> ( sys );
    const double a = 0.5 * alpha * delt;

    for ( int i = 0; i < 7; ++i )
        A_stencil[id][i] = -a * lap_stn[i];
    A_stencil[id].p += 1;
    for ( auto & entry : lap_add )
        *A_additional += -a * entry;
    b[id] = u_old[id] + a * ( rhs + u_old.laplacian ( id ) );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_crank_nicolson_assemble_hypresstruct_rhs
(
    const IntVector & id,
    const FDView < ScalarField<const double>, STN > & u_old,
    View < ScalarField<double> > & b
)
{
    double rhs = u_old.laplacian_rhs_hypresstruct ( id );
    const double a = 0.5 * alpha * delt;

    b[id] = u_old[id] + a * ( rhs + u_old.laplacian ( id ) );
}

#   ifdef PhaseField_Heat_DBG_MATRIX
template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void Heat<VAR, DIM, STN, AMR, TST>::time_advance_update_dbg_matrix
(
    const IntVector & id,
    View < ScalarField<const Stencil7> > & A,
    View < ScalarField<double> > & Ap,
    View < ScalarField<double> > & Aw,
    View < ScalarField<double> > & Ae,
    View < ScalarField<double> > & As,
    View < ScalarField<double> > & An,
    View < ScalarField<double> > & Ab,
    View < ScalarField<double> > & At
)
{
    Ap[id] = A[id].p;
    Aw[id] = A[id].w;
    Ae[id] = A[id].e;
    As[id] = A[id].s;
    An[id] = A[id].n;
    At[id] = A[id].t;
    Ab[id] = A[id].b;
}
#   endif
#endif

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_epsilon
(
    const IntVector & id,
    const Patch * patch,
    const double & ut,
    const double & a,
    const View < ScalarField<const double> > & u,
    View < ScalarField<double> > & epsilon_u
)
{
    Vector v ( this->get_position ( patch, id ).asVector() );
    double u_ex = ut;

    for ( size_t i = 0; i < DIM; ++i )
        u_ex *= cos ( a * v[i] );

    epsilon_u[id] = u[id] - u_ex;
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_epsilon
(
    const IntVector & id,
    const Patch * patch,
    const double & ut,
    const double & a,
    const View < ScalarField<const double> > & u,
    View < ScalarField<double> > & epsilon_u
)
{
    if ( this->is_refined ( patch->getLevel(), patch, id ) )
        epsilon_u[id] = 0;
    else
        time_advance_solution_epsilon < !MG > ( id, patch, ut, a, u, epsilon_u );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < !MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_error
(
    const IntVector & id,
    const Patch * patch,
    const double & ut,
    const double & a,
    const double & area,
    const double & factor,
    const View < ScalarField<const double> > & u,
    View < ScalarField<double> > & error_u,
    double & u_norm2_L2,
    double & error_norm2_L2
)
{
    double u2 = 0.;
    double u_err2 = 0.;

    auto do_loop = [this, &patch, &ut, &a, &u, &area, &u2, &u_err2] ( IntVector id, double w = 1. )
    {
        Vector v = this->get_position ( patch, id ).asVector();
        double u_ex = ut;

        for ( size_t i = 0; i < DIM; ++i )
            u_ex *= cos ( a * v[i] );

        double uf = u[id];
        double eu = uf - u_ex;

#ifdef EXACT_NORM
        u2 += w * area * u_ex * u_ex;
#else
        u2 += w * area * uf * uf;
#endif
        u_err2 += w * area * eu * eu;
    };

    if ( VAR == CC )
    {
        do_loop ( id );
    }
    else
    {
        IntVector offset;
        for ( offset[X] = 0; offset[X] < 2; ++offset[X] )
            for ( offset[Y] = 0; offset[Y] < 2; ++offset[Y] )
                for ( offset[Z] = 0; offset[Z] < 2; ++offset[Z] )
                    do_loop ( id + offset, 0.125 );
    }

    u_norm2_L2 += u2;
    error_norm2_L2 += u_err2;

    error_u[id] = sqrt ( u_err2 / ( factor * area ) );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template < bool MG >
typename std::enable_if < MG, void >::type
Heat<VAR, DIM, STN, AMR, TST>::time_advance_solution_error
(
    const IntVector & id,
    const Patch * patch,
    const Patch * patch_finest,
    const IntVector & refinement,
    const double & ut,
    const double & a,
    const double & area,
    const double & factor,
    const View < ScalarField<const double> > & u_finest,
    View < ScalarField<double> > & error_u,
    double & u_norm2_L2,
    double & error_norm2_L2
)
{
    const Level * level ( patch->getLevel() );

    if ( this->is_refined ( level, patch, id ) )
    {
        error_u[id] = 0;
        return;
    }

    double u2 = 0.;
    double u_err2 = 0.;

    auto do_loop = [this, &patch_finest, &ut, &a, &u_finest, &area, &u2, &u_err2] ( IntVector idf, double w = 1. )
    {
        Vector v = this->get_position ( patch_finest, idf ).asVector();
        double u_ex = ut;

        for ( size_t i = 0; i < DIM; ++i )
            u_ex *= cos ( a * v[i] );

        double uf = u_finest[idf];
        double eu = uf - u_ex;
#ifdef EXACT_NORM
        u2 += w * area * u_ex * u_ex;
#else
        u2 += w * area * uf * uf;
#endif
        u_err2 += w * area * eu * eu;
    };

    IntVector id_finest = id * refinement;
    IntVector offset;

    if ( VAR == CC )
    {
        for ( offset[X] = 0; offset[X] < refinement[X]; ++offset[X] )
            for ( offset[Y] = 0; offset[Y] < refinement[Y]; ++offset[Y] )
                for ( offset[Z] = 0; offset[Z] < refinement[Z]; ++offset[Z] )
                    do_loop ( id_finest + offset );
    }
    else
    {
        offset[X] = 0;
        {
            offset[Y] = 0;
            {
                offset[Z] = 0;
                {
                    do_loop ( id_finest + offset, 0.125 );
                }
                for ( offset[Z] = 1; offset[Z] < refinement[Z]; ++offset[Z] )
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
                offset[Z] = refinement[Z];
                {
                    do_loop ( id_finest + offset, 0.125 );
                }
            }
            for ( offset[Y] = 1; offset[Y] < refinement[Y]; ++offset[Y] )
            {
                offset[Z] = 0;
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
                for ( offset[Z] = 1; offset[Z] < refinement[Z]; ++offset[Z] )
                {
                    do_loop ( id_finest + offset, 0.5 );
                }
                offset[Z] = refinement[Z];
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
            }
            offset[Y] = refinement[Y];
            {
                offset[Z] = 0;
                {
                    do_loop ( id_finest + offset, 0.125 );
                }
                for ( offset[Z] = 1; offset[Z] < refinement[Z]; ++offset[Z] )
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
                offset[Z] = refinement[Z];
                {
                    do_loop ( id_finest + offset, 0.125 );
                }
            }
        }
        for ( offset[X] = 1; offset[X] < refinement[X]; ++offset[X] )
        {
            offset[Y] = 0;
            {
                offset[Z] = 0;
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
                for ( offset[Z] = 1; offset[Z] < refinement[Z]; ++offset[Z] )
                {
                    do_loop ( id_finest + offset, 0.5 );
                }
                offset[Z] = refinement[Z];
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
            }
            for ( offset[Y] = 1; offset[Y] < refinement[Y]; ++offset[Y] )
            {
                offset[Z] = 0;
                {
                    do_loop ( id_finest + offset, 0.5 );
                }
                for ( offset[Z] = 1; offset[Z] < refinement[Z]; ++offset[Z] )
                {
                    do_loop ( id_finest + offset, 1. );
                }
                offset[Z] = refinement[Z];
                {
                    do_loop ( id_finest + offset, 0.5 );
                }
            }
            offset[Y] = refinement[Y];
            {
                offset[Z] = 0;
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
                for ( offset[Z] = 1; offset[Z] < refinement[Z]; ++offset[Z] )
                {
                    do_loop ( id_finest + offset, 0.5 );
                }
                offset[Z] = refinement[Z];
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
            }
        }
        offset[X] = refinement[X];
        {
            offset[Y] = 0;
            {
                offset[Z] = 0;
                {
                    do_loop ( id_finest + offset, 0.125 );
                }
                for ( offset[Z] = 1; offset[Z] < refinement[Z]; ++offset[Z] )
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
                offset[Z] = refinement[Z];
                {
                    do_loop ( id_finest + offset, 0.125 );
                }
            }
            for ( offset[Y] = 1; offset[Y] < refinement[Y]; ++offset[Y] )
            {
                offset[Z] = 0;
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
                for ( offset[Z] = 1; offset[Z] < refinement[Z]; ++offset[Z] )
                {
                    do_loop ( id_finest + offset, 0.5 );
                }
                offset[Z] = refinement[Z];
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
            }
            offset[Y] = refinement[Y];
            {
                offset[Z] = 0;
                {
                    do_loop ( id_finest + offset, 0.125 );
                }
                for ( offset[Z] = 1; offset[Z] < refinement[Z]; ++offset[Z] )
                {
                    do_loop ( id_finest + offset, 0.25 );
                }
                offset[Z] = refinement[Z];
                {
                    do_loop ( id_finest + offset, 0.125 );
                }
            }
        }
    }

    u_norm2_L2 += u2;
    error_norm2_L2 += u_err2;

    error_u[id] = sqrt ( u_err2 / ( factor * area ) );
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::refine_solution
(
    const IntVector id_fine,
    const View < ScalarField<const double> > & u_coarse_interp,
    View < ScalarField<double> > & u_fine
)
{
    u_fine[id_fine] = u_coarse_interp[id_fine];
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
void
Heat<VAR, DIM, STN, AMR, TST>::coarsen_solution
(
    const IntVector id_coarse,
    const View < ScalarField<const double> > & u_fine_restr,
    View < ScalarField<double> > & u_coarse
)
{
    u_coarse[id_coarse] = u_fine_restr[id_coarse];
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template<VarType V>
typename std::enable_if < V == CC, void >::type
Heat<VAR, DIM, STN, AMR, TST>::error_estimate_solution
(
    const IntVector & id,
    const double & r2,
    FDView < ScalarField<const double>, STN > & u,
    View < ScalarField<int> > & refine_flag
)
{
    bool refine = false;
    auto grad = u.gradient ( id );
    double err2 = 0;
    for ( size_t d = 0; d < DIM; ++d )
        err2 += grad[d] * grad[d];
    refine = err2 > r2;
    refine_flag[id] = refine;
}

template<VarType VAR, DimType DIM, StnType STN, bool AMR, bool TST>
template<VarType V>
typename std::enable_if < V == NC, void >::type
Heat<VAR, DIM, STN, AMR, TST>::error_estimate_solution
(
    const IntVector & id,
    const double & r2,
    FDView < ScalarField<const double>, STN > & u,
    View < ScalarField<int> > & refine_flag
)
{
    auto grad = u.gradient ( id );
    double err2 = 0;
    for ( size_t d = 0; d < DIM; ++d )
        err2 += grad[d] * grad[d];
    if ( err2 > r2 )
    {
        // loop over all cells sharing node id
        IntVector id0 = id - get_dim<DIM>::unit_vector();
        IntVector i;
        for ( i[Z] = id0[Z]; i[Z] <= id[Z]; ++i[Z] )
            for ( i[Y] = id0[Y]; i[Y] <= id[Y]; ++i[Y] )
                for ( i[X] = id0[X]; i[X] <= id[X]; ++i[X] )
                    if ( refine_flag.is_defined_at ( i ) )
                        refine_flag[i] = 1;
    }
}

} // namespace PhaseField
} // namespace Uintah

#endif // Packages_Uintah_CCA_Components_PhaseField_Applications_Heat_h

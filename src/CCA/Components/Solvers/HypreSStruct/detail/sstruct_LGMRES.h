/*
 * The MIT License
 *
 * Copyright (c) 1997-2019 The University of Utah
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

#ifndef Packages_Uintah_CCA_Components_Solvers_HypreSStruct_detail_sstructLGMRES_h
#define Packages_Uintah_CCA_Components_Solvers_HypreSStruct_detail_sstructLGMRES_h

#include <CCA/Components/Solvers/HypreSStruct/detail/sstruct_implementation.h>

#include <CCA/Components/Solvers/HypreSStruct/SolverParams.h>
#include <CCA/Components/Solvers/HypreSStruct/SolverOutput.h>

#include <HYPRE_krylov.h>

namespace Uintah
{
namespace HypreSStruct
{
namespace detail
{

template<int DIM, int C2F, bool precond>
class sstruct_solver< ( int ) S::LGMRES, DIM, C2F, precond>
    : public sstruct_implementation<DIM, C2F>
{
protected:
    using sstruct = sstruct_implementation<DIM, C2F>;

    HYPRE_Solver * psolver, & solver;
    HYPRE_Matrix * pA, & A;
    HYPRE_Vector * pb, & b;
    HYPRE_Vector * px, & x;

    using sstruct::solver_initialized;
    using sstruct::guess_updated;

public: // STATIC MEMBERS
    static constexpr auto SetPrecond = HYPRE_LGMRESSetPrecond;

    static const HYPRE_PtrToSolverFcn precond_solve;
    static const HYPRE_PtrToSolverFcn precond_setup;

    sstruct_solver (
        const GlobalDataP & gdata
    ) : sstruct ( gdata ),
        psolver ( reinterpret_cast<HYPRE_Solver *> ( & ( sstruct::solver ) ) ), solver ( *psolver ),
        pA ( reinterpret_cast<HYPRE_Matrix *> ( & ( sstruct::A ) ) ), A ( *pA ),
        pb ( reinterpret_cast<HYPRE_Vector *> ( & ( sstruct::b ) ) ), b ( *pb ),
        px ( reinterpret_cast<HYPRE_Vector *> ( & ( sstruct::x ) ) ), x ( *px )
    {
    }

    virtual ~sstruct_solver()
    {
        solverFinalize();
    }

    virtual void
    solverInitialize (
        const MPI_Comm & comm,
        const SolverParams * params
    ) override
    {
        ASSERT ( !solver_initialized );

        IGNOREPARAM ( SStructLGMRES, params, csolver_type );
        IGNOREPARAM ( SStructLGMRES, params, max_levels );
        IGNOREPARAM ( SStructLGMRES, params, num_post_relax );
        IGNOREPARAM ( SStructLGMRES, params, num_pre_relax );
        IGNOREPARAM ( SStructLGMRES, params, relax_type );
        IGNOREPARAM ( SStructLGMRES, params, skip_relax );
        IGNOREPARAM ( SStructLGMRES, params, ssolver );
        IGNOREPARAM ( SStructLGMRES, params, weight );

        HYPRE_SStructLGMRESCreate ( comm, & ( sstruct::solver ) );

        if ( params->tol != -1 )
            HYPRE_LGMRESSetTol ( solver, params->tol );
        if ( params->abs_tol != -1 )
            HYPRE_LGMRESSetAbsoluteTol ( solver, params->abs_tol );
        if ( params->cf_tol != -1 )
            HYPRE_LGMRESSetConvergenceFactorTol ( solver, params->cf_tol );
        if ( params->min_iter != -1 )
            HYPRE_LGMRESSetMinIter ( solver, params->min_iter );
        if ( params->max_iter != -1 )
            HYPRE_LGMRESSetMaxIter ( solver, params->max_iter );
        if ( params->two_norm != -1 )
            HYPRE_LGMRESSetKDim ( solver, params->k_dim );
        if ( params->two_norm != -1 )
            HYPRE_LGMRESSetAugDim ( solver, params->aug_dim );
        if ( params->logging != -1 )
            HYPRE_LGMRESSetLogging ( solver, params->logging );

        solver_initialized = true;
    }

    virtual void
    solverUpdate (
    ) override
    {
        HYPRE_LGMRESSetup ( solver, A, b, x );
    }

    virtual void
    solve (
        SolverOutput * out
    ) override
    {
        HYPRE_LGMRESSolve ( solver, A, b, x );
        HYPRE_LGMRESGetNumIterations ( solver, &out->num_iterations );
        HYPRE_LGMRESGetFinalRelativeResidualNorm ( solver, &out->res_norm );
        guess_updated = false;
    }

public: // exposing this for when used as precond

    virtual void
    solverFinalize()
    override
    {
        if ( solver_initialized ) HYPRE_SStructLGMRESDestroy ( sstruct::solver );
        solver_initialized = false;
    }

public: // required if precond

    operator HYPRE_Solver ()
    {
        return ( HYPRE_Solver ) solver;
    }
};

template<int DIM, int C2F, bool precond> const HYPRE_PtrToSolverFcn sstruct_solver< ( int ) S::LGMRES, DIM, C2F, precond>::precond_solve = ( HYPRE_PtrToSolverFcn ) HYPRE_LGMRESSolve;
template<int DIM, int C2F, bool precond> const HYPRE_PtrToSolverFcn sstruct_solver< ( int ) S::LGMRES, DIM, C2F, precond>::precond_setup = ( HYPRE_PtrToSolverFcn ) HYPRE_LGMRESSetup;

} // namespace detail
} // namespace HypreSStruct
} // namespace Uintah

#endif // Packages_Uintah_CCA_Components_Solvers_HypreSStruct_detail_sstructLGMRES_h

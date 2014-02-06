/// \file oseenprecond.h
/// \brief preconditioners for the oseen problem and the schur complement
/// \author LNM RWTH Aachen: Patrick Esser, Joerg Grande, Sven Gross, Helmut Jarausch, Volker Reichelt; SC RWTH Aachen:

/*
 * This file is part of DROPS.
 *
 * DROPS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DROPS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with DROPS. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Copyright 2012 LNM/SC RWTH Aachen, Germany
*/

#include "num/precond.h"
#include "num/MGsolver.h"
#include "num/spblockmat.h"
#include "misc/scopetimer.h"

#ifndef OSEENPRECOND_H_
#define OSEENPRECOND_H_

namespace DROPS
{

/// fwd decl from num/stokessolver.h
template<typename, typename, typename>
class ApproximateSchurComplMatrixCL;
template<typename, typename, typename>
class SchurComplMatrixCL;

/// base class for Schur complement preconditioners
class SchurPreBaseCL
{
  protected:
    double kA_,   ///< scaling factor for pressure stiffness matrix or equivalent
           kM_;   ///< scaling factor for pressure mass matrix
    std::ostream* output_;

  public:
    SchurPreBaseCL( double kA, double kM, std::ostream* output= 0) : kA_( kA), kM_( kM), output_(output) {}
    virtual ~SchurPreBaseCL() {}
    void SetWeights( double kA, double kM) { kA_ = kA; kM_ = kM; }

#ifdef _PAR
    virtual void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const = 0;
    virtual void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const = 0;
#endif
    virtual void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const = 0;
    virtual void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const = 0;


    // dummy Apply routine for inexact Uzawa. The matrix parameter in the Apply is not used, anyway.
    template<typename PcT, typename Mat, typename ExT>
    void Apply( const ApproximateSchurComplMatrixCL<PcT, Mat, ExT>& A, VectorCL& x, const VectorCL& b, const ExT& p_ex) const { const Mat dummy; Apply( dummy, x, b, A.GetVelEx(), p_ex); }
    // dummy Apply routine for inexact Uzawa. The matrix parameter in the Apply is not used, anyway.
    template<typename PcT, typename Mat, typename ExT>
    void Apply( const SchurComplMatrixCL<PcT, Mat, ExT>& A, VectorCL& x, const VectorCL& b, const ExT& p_ex) const { const Mat dummy; Apply( dummy, x, b, A.GetVelEx(), p_ex); }

    /// \name Parallel preconditioner setup ...
    //@{
    bool NeedDiag() const { return false; }
    void SetDiag(const VectorCL&) {}        // just for consistency
    template<typename Mat, typename ExT>
    void SetDiag(const Mat&, const ExT&) {} // just for consistency
    bool RetAcc()   const { return true; }
    //@}
};

//**************************************************************************
// Preconditioner for the instationary Stokes-equations.
// cf. "Iterative Techniques For Time Dependent Stokes Problems",
//     James H. Bramble, Joseph E. Pasciak, January 1994
//
// A Poisson-problem with natural boundary-conditions for the pressure is
// solved via 1 SSOR-step, a problem with the mass-matrix as well.
// The constants kA_, kM_ have to be chosen according to h and dt, see Theorem 4.1
// of the above paper.
// kA_ = theta/Re and kM_ = 1/dt will do a good job,
// where Re is proportional to the ratio density/viscosity.
//
// A_ is the pressure-Poisson-Matrix for natural boundary-conditions, M_ the
// pressure-mass-matrix.
//**************************************************************************
class ISPreCL : public SchurPreBaseCL
{
  private:
    MatrixCL& A_;
    MatrixCL& M_;
    SSORPcCL  ssor_;

  public:
    ISPreCL( MatrixCL& A_pr, MatrixCL& M_pr,
        double kA= 0., double kM= 1., double om= 1.)
        : SchurPreBaseCL( kA, kM), A_( A_pr), M_( M_pr), ssor_( om)  {}
    ISPreCL( MLMatrixCL& A_pr, MLMatrixCL& M_pr,
             double kA= 0., double kM= 1., double om= 1.)
    : SchurPreBaseCL( kA, kM), A_( A_pr.GetFinest()), M_( M_pr.GetFinest()), ssor_( om)  {}

    /// \brief Apply preconditioner
    template <typename Mat, typename Vec, typename ExT>
    void Apply(const Mat&, Vec& p, const Vec& c, const ExT& vel_ex, const ExT& pr_ex) const;
#ifdef _PAR
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
#endif
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }

    using SchurPreBaseCL::Apply;
};

template <typename Mat, typename Vec, typename ExT>
void ISPreCL::Apply(const Mat&, Vec& p, const Vec& c, const ExT&, const ExT& pr_ex) const
{
//    double new_res;
//    double old_res= norm( c);
    ssor_.Apply( A_, p, c, pr_ex);
//    std::cout << " residual: " <<  (new_res= norm( A_*p - c)) << '\t';
//    std::cout << " reduction: " << new_res/old_res << '\t';
    p*= kA_;
//    double mnew_res;
//    double mold_res= norm( c);
    Vec p2_( c.size());
    ssor_.Apply( M_, p2_, c, pr_ex);
//    std::cout << " residual: " <<  (mnew_res= norm( M_*p2_ - c)) << '\t';
//    std::cout << " reduction: " << mnew_res/mold_res << '\n';
    p+= kM_*p2_;
}

//**************************************************************************
// Preconditioner for the instationary Stokes-equations.
// Confer ISPreCL for details. This preconditioner uses a few CG-iterations
// to solve the linear systems.
// It is well suited for InexactUzawa-Solvers.
//**************************************************************************
template <typename SolverT>
class ISNonlinearPreCL : public SchurPreBaseCL
{
  private:
    MatrixCL&  A_;
    MatrixCL&  M_;
    SolverT&   Asolver_;
    SolverT&   Msolver_;

  public:
    ISNonlinearPreCL(SolverT& Asolver, SolverT& Msolver, MatrixCL& A_pr, MatrixCL& M_pr,
        double kA= 0., double kM= 1.)
        : SchurPreBaseCL( kA, kM), A_( A_pr), M_( M_pr), Asolver_( Asolver), Msolver_( Msolver)  {}

    /// \brief Apply preconditioner
    template <typename Mat, typename Vec, typename ExT>
    void Apply(const Mat&, Vec& p, const Vec& c, const ExT& vel_ex, const ExT& pr_ex) const;
#ifdef _PAR
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
#endif
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }

    using SchurPreBaseCL::Apply;
};

template <class SolverT>
template <typename Mat, typename Vec, typename ExT>
void ISNonlinearPreCL<SolverT>::Apply(const Mat&, Vec& p, const Vec& c, const ExT&, const ExT& ex) const
{
    p= 0.0;
    if (kA_ != 0.0) {
        if (Asolver_.GetPc().NeedDiag())
            Asolver_.GetPc().SetDiag(A_, ex);
        Asolver_.Solve( A_, p, c, ex);
        if (Asolver_.GetIter() == Asolver_.GetMaxIter())
        std::cout << "ISNonlinearPreCL::Apply (1st solve: iterations: " << Asolver_.GetIter()
                  << "\tresidual: " <<  Asolver_.GetResid() << '\n';
        p*= kA_;
    }
    if ( kM_ != 0.0) {
        Vec p2_( c.size());
        if (Msolver_.GetPc().NeedDiag())
            Msolver_.GetPc().SetDiag(M_, ex);
        Msolver_.Solve( M_, p2_, c, ex);
        if (Msolver_.GetIter() == Msolver_.GetMaxIter())
        std::cout << "ISNonlinearPreCL::Apply (2nd solve: iterations: " << Msolver_.GetIter()
                  << "\tresidual: " <<  Msolver_.GetResid() << '\n';
        p+= kM_*p2_;
    }
}


/* with orthogonalization
template <typename Mat, typename Vec>
void ISNonlinearPreCL::Apply(const Mat&, Vec& p, const Vec& c) const
{
    VectorCL e( 1., p.size());
    const double ee= p.size(); // = dot( e, e);
    VectorCL c2( (dot(c, e)/ee)*e);
    VectorCL c3( c - c2);
std::cout << "norm( c): " << norm( c) << "\tnorm( e): " << norm( e)
          << "\tdot(c, e)/norm( c)/norm( e): " << dot(c, e)/norm( c)/ee << '\n';
    p= 0.0;
    solver_.Solve( A_, p, c3);
    p+= c2;
    std::cout << "ISNonlinearPreCL p: iterations: " << solver_.GetIter()
              << "\tresidual: " <<  solver_.GetResid();
    p*= kA_;
    if ( kM_ != 0.0) {
        Vec p2_( c.size());
        solver_.Solve( M_, p2_, c);
std::cout << "norm( p2): " << norm( p2_);
        std::cout << "\t p2: iterations: " << solver_.GetIter()
                  << "\tresidual: " <<  solver_.GetResid()
                  << '\n';
        p+= kM_*p2_;
    }
}
*/

//**************************************************************************
// Preconditioner for the instationary Stokes-equations.
// cf. "Iterative Techniques For Time Dependent Stokes Problems",
//     James H. Bramble, Joseph E. Pasciak, January 1994
//
// Confer ISPreCL for details regarding preconditioning of S. This
// preconditioner uses multigrid-solvers.
//
//**************************************************************************
template< class ProlongationT>
class ISMGPreCL : public SchurPreBaseCL
{
  private:
    const Uint sm; // how many smoothing steps?
    const int lvl; // how many levels? (-1=all)
    const double omega; // relaxation parameter for smoother
    typedef SSORsmoothCL SmootherT;
    MLSmootherCL<SmootherT> smoother;  // Symmetric-Gauss-Seidel with over-relaxation
    SSORPcCL directpc;
    mutable PCGSolverCL<SSORPcCL> solver;

    DROPS::MLMatrixCL& Apr_;
    DROPS::MLMatrixCL& Mpr_;
    ProlongationT      P_;
    const MLIdxDescCL& idx_;
    DROPS::Uint iter_prA_;
    DROPS::Uint iter_prM_;
    mutable std::vector<DROPS::VectorCL> ones_;

    void MaybeInitOnes() const;

  public:
    ISMGPreCL(DROPS::MLMatrixCL& A_pr, DROPS::MLMatrixCL& M_pr,
                    double kA, double kM, const MLIdxDescCL& idx, DROPS::Uint iter_prA=1,
                    DROPS::Uint iter_prM = 1)
        : SchurPreBaseCL( kA, kM), sm( 1), lvl( -1), omega( 1.0), smoother( omega), solver( directpc, 200, 1e-12),
          Apr_( A_pr), Mpr_( M_pr), idx_(idx), iter_prA_( iter_prA), iter_prM_( iter_prM), ones_(0)
    {
        smoother.resize( idx.size(), SmootherT(omega));
    }

    /// \brief Apply preconditioner
    template <typename Mat, typename Vec, typename ExT>
    void Apply(const Mat&, Vec& p, const Vec& c, const ExT& vel_ex, const ExT& pr_ex) const;
#ifdef _PAR
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
#endif
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }

    using SchurPreBaseCL::Apply;
    ProlongationT* GetProlongation() { return &P_; }
};

template<class ProlongationT>
void ISMGPreCL<ProlongationT>::MaybeInitOnes() const
{
    if (Mpr_.size() == ones_.size()) return;
    // Compute projection on constant pressure function only once.
    Uint i= 0;
    ones_.resize(0); // clear all
    ones_.resize(Mpr_.size());
    for (MLMatrixCL::const_iterator it= Mpr_.begin(); it != Mpr_.end(); ++it, ++i) {
        ones_[i].resize( it->num_cols(), 1.0/it->num_cols());
    }
}

template<class ProlongationT>
template <typename Mat, typename Vec, typename ExT>
void ISMGPreCL<ProlongationT>::Apply(const Mat& /*A*/, Vec& p, const Vec& c, const ExT&, const ExT&) const
{
    MaybeInitOnes();
    p= 0.0;
    const Vec c2_( c - dot( ones_.back(), c));
    typename ProlongationT::const_iterator finestP = --P_.end();
    MLIdxDescCL::const_iterator finestIdx = idx_.GetFinestIter();
    MLSmootherCL<SmootherT>::const_iterator smootherit = smoother.GetFinestIter();
//    double new_res= norm(Apr_*p - c);
//    double old_res;
//    std::cout << "Pressure: iterations: " << iter_prA_ <<'\t';
    for (DROPS::Uint i=0; i<iter_prA_; ++i) {
        DROPS::MGMPr( ones_.end()-1, Apr_.begin(), --Apr_.end(), finestP, p, c2_, finestIdx, smootherit, sm, solver, lvl, -1);
//        old_res= new_res;
//        std::cout << " residual: " <<  (new_res= norm(Apr_*p - c)) << '\t';
//        std::cout << " reduction: " << new_res/old_res << '\n';
    }
    p*= kA_;
//    std::cout << " residual: " <<  norm(Apr_*p - c) << '\t';

    Vec p2( p.size());
    for (DROPS::Uint i=0; i<iter_prM_; ++i)
        DROPS::MGM( Mpr_.begin(), --Mpr_.end(), finestP, p2, c, finestIdx, smootherit, sm, solver, lvl, -1);
//    std::cout << "Mass: iterations: " << iter_prM_ << '\t'
//              << " residual: " <<  norm(Mpr_*p2 - c) << '\n';

    p+= kM_*p2;
}

#ifndef _PAR
// Append the kernel of Bs as last column to Bs.
template<typename ExT>
static void Regularize (MatrixCL& Bs, const IdxDescCL& rowidx, VectorCL ker0, const NEGSPcCL& spc, double regularize, const ExT& row_ex, const ExT& col_ex)
{
    if (rowidx.IsExtended())
        ker0[std::slice( rowidx.GetXidx().GetNumUnknownsStdFE(), rowidx.NumUnknowns() - rowidx.GetXidx().GetNumUnknownsStdFE(), 1)]= 0.;
    ker0*= 1./norm( ker0);
    VectorCL ker( spc.mul( Bs, ker0, row_ex, col_ex));
    ker*= regularize/std::sqrt( dot( ker, ker0));
    Bs.insert_col( Bs.num_cols(), ker);
}
#endif

//**************************************************************************
// Preconditioner for the instationary Stokes-equations.
// It uses BB^T instead of a Laplacian on the pressure space and is
// therefore suited for P1X-elements;
// cf. "Uniform Preconditioners for a Parameter Dependent Saddle Point
// Problem with Application to Generalized Stokes Interface Equations",
// Olshanskii, Peters, Reusken, 2005
//**************************************************************************
class ISBBTPreCL : public SchurPreBaseCL
{
  private:
    const MatrixCL*  B_;
    mutable MatrixCL*  Bs_;                                     ///< scaled Matrix B
    mutable size_t Bversion_;
    const MatrixCL*  M_, *Mvel_;

    double     tolA_, tolM_;                                    ///< tolerances of the solvers
    mutable VectorCL Dprsqrtinv_;                               ///< diag(M)^{-1/2}

#ifdef _PAR
    typedef ChebyshevBBTPcCL  PCSolver1T;                       ///< type of the preconditioner for solver 1
#else
    typedef NEGSPcCL    PCSolver1T;                             ///< type of the preconditioner for solver 1
#endif
    typedef JACPcCL     PCSolver2T;                             ///< type of the preconditioner for solver 2
    PCSolver1T PCsolver1_;
    PCSolver2T PCsolver2_;
    mutable PCGNESolverCL<PCSolver1T> solver_;                  ///< solver for BB^T
    mutable PCGSolverCL<PCSolver2T> solver2_;                   ///< solver for M

    const IdxDescCL* pr_idx_;                                   ///< used to determine, how to represent the kernel of BB^T in case of pure Dirichlet-BCs.
    double regularize_;                                         ///< If regularize_==0. no regularization is performed. Otherwise, a column is attached to Bs.
    template <typename ExT>
    void Update (const ExT& vel_ex, const ExT& p_ex) const;     ///< Updating the diagonal matrices D and Dprsqrtinv

  public:
    ISBBTPreCL (const MatrixCL* B, const MatrixCL* M_pr, const MatrixCL* Mvel,
        const IdxDescCL& pr_idx, double kA= 0., double kM= 1., double tolA= 1e-2, double tolM= 1e-2, double regularize= 0., std::ostream* output= 0)
        : SchurPreBaseCL( kA, kM, output), B_( B), Bs_( 0), Bversion_( 0),
          M_( M_pr), Mvel_( Mvel), tolA_(tolA), tolM_(tolM),
          PCsolver1_(), PCsolver2_(),
          solver_( PCsolver1_, 800, tolA_, /*relative*/ true),
          solver2_( PCsolver2_, 500, tolM_, /*relative*/ true),
          pr_idx_( &pr_idx), regularize_( regularize) {}
    ISBBTPreCL (const ISBBTPreCL& pc)
        : SchurPreBaseCL( pc.kA_, pc.kM_), B_( pc.B_), Bs_( pc.Bs_ == 0 ? 0 : new MatrixCL( *pc.Bs_)),
          Bversion_( pc.Bversion_),
          M_( pc.M_), Mvel_( pc.Mvel_),
          tolA_(pc.tolA_), tolM_(pc.tolM_),
          Dprsqrtinv_( pc.Dprsqrtinv_),
          PCsolver1_(), PCsolver2_(),
          solver_( PCsolver1_, 800, tolA_, /*relative*/ true),
          solver2_( PCsolver2_, 500, tolM_, /*relative*/ true),
          pr_idx_( pc.pr_idx_), regularize_( pc.regularize_){}

    ISBBTPreCL& operator= (const ISBBTPreCL&) {
        throw DROPSErrCL( "ISBBTPreCL::operator= is not permitted.\n");
    }

    ~ISBBTPreCL () { delete Bs_; }

    template <typename Mat, typename Vec, typename ExT>
    void Apply(const Mat&, Vec& p, const Vec& c, const ExT& vel_ex, const ExT& p_ex) const;

#ifdef _PAR
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
#endif
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }

    using SchurPreBaseCL::Apply;

    void SetMatrices (const MatrixCL* B, const MatrixCL* Mvel, const MatrixCL* M, const IdxDescCL* pr_idx)
    {
        B_= B;
        Mvel_= Mvel;
        M_= M;
        pr_idx_= pr_idx;
        Bversion_ = 0;
    }
};

template<typename ExT>
void ISBBTPreCL::Update(const ExT& vel_ex, const ExT& p_ex) const
{
    std::cout << "ISBBTPreCL::Update: old version: " << Bversion_
              << "\tnew version: " << B_->Version() << '\n';
    delete Bs_;
    Bs_= new MatrixCL( *B_);
    Bversion_= B_->Version();

    VectorCL Dvelinv( 1.0/ vel_ex.GetAccumulate(Mvel_->GetDiag()));
    ScaleCols( *Bs_, VectorCL( std::sqrt( Dvelinv)));

    VectorCL Dprsqrt( std::sqrt( p_ex.GetAccumulate( M_->GetDiag())));
    Dprsqrtinv_.resize( M_->num_rows());
    Dprsqrtinv_= 1.0/Dprsqrt;
    ScaleRows( *Bs_, Dprsqrtinv_);

#ifndef _PAR
    if (regularize_ != 0.)
        Regularize( *Bs_, *pr_idx_, Dprsqrt, PCsolver1_, regularize_, vel_ex, p_ex);
#endif
}

template <typename Mat, typename Vec, typename ExT>
void ISBBTPreCL::Apply(const Mat&, Vec& p, const Vec& c, const ExT& vel_ex, const ExT& p_ex) const
{
    ScopeTimerCL scope("ISBBTPreCL::Apply");
    if (B_->Version() != Bversion_)
        Update(vel_ex, p_ex);

    p= 0.0;
    if (kA_ != 0.0) {
        solver_.Solve( *Bs_, p, VectorCL( Dprsqrtinv_*c), vel_ex, p_ex);
        if (solver_.GetIter() == solver_.GetMaxIter()){
            std::cout << "ISBBTPreCL::Apply: BBT-solve: " << solver_.GetIter()
                    << " (max)\t" << solver_.GetResid() << '\n';
        }
        else if (output_)
            *output_ << "ISBBTPreCL BBT-solve: iterations: " << solver_.GetIter()
                     << "\tresidual: " <<  solver_.GetResid();
        p= kA_*(Dprsqrtinv_*p);
    }
    if (kM_ != 0.0) {
        Vec p2_( c.size());
        solver2_.Solve( *M_, p2_, c, p_ex);
        if (solver2_.GetIter() == solver2_.GetMaxIter()){
            std::cout << "ISBBTPreCL::Apply: M-solve: " << solver2_.GetIter()
                    << " (max)\t" << solver2_.GetResid() << '\n';
        }
        else if (output_)
            *output_ << "\tISBBTPreCL M-solve: iterations: " << solver2_.GetIter()
                     << "\tresidual: " <<  solver2_.GetResid()
                     << '\n';

        p+= kM_*p2_;
    }
}

/*!
 * \brief Modified ISBBT preconditioner for Ghost Penalty Stabilisation
 *
 * This preconditioner is an extension to the original ISBBT preconditioner
 * for the instationary Stokes problem. When the ghost penalty method is used
 * for stabilising the Stokes problem, the resulting system matrix contains an
 * additional matrix -eps_p*J. The Schurkomplement therefore changes to:
 * B^T A^-1 B  +  eps_p*J. Given C := -eps_p*J, this preconditoner is defined
 * by Q_S := M - C = M + eps_p*J. In the stationary case this preconditioner
 * has shown ideal behaviour. The instationary case requires further testing.
 */
class ISBBT_Stab_PreCL : public SchurPreBaseCL
{
private:
    const MatrixCL*  B_;
    mutable MatrixCL*  Bs_;                                     ///< scaled Matrix B
    mutable size_t Bversion_;
    const MatrixCL  *M_, *Mvel_;
    const MatrixCL  *C_;                                        ///< Stabilisation matrix; C = -eps_p * prJ;
    mutable MatrixCL   MminusC_;                                ///< Use M - C instead of M as a preconditioner of BA^-1B^T

    double  tolA_, tolM_;                                       ///< tolerances of the solvers
    mutable VectorCL Dprsqrtinv_;                               ///< diag(M)^{-1/2}

#ifdef _PAR
    typedef ChebyshevBBTPcCL  PCSolver1T;                       ///< type of the preconditioner for solver 1
#else
    typedef NEGSPcCL    PCSolver1T;                             ///< type of the preconditioner for solver 1
#endif
    typedef JACPcCL     PCSolver2T;                             ///< type of the preconditioner for solver 2
    PCSolver1T PCsolver1_;
    PCSolver2T PCsolver2_;
    mutable PCGNESolverCL<PCSolver1T> solver_;                  ///< solver for BB^T
    mutable PCGSolverCL<PCSolver2T> solver2_;                   ///< solver for M

    const IdxDescCL* pr_idx_;                                   ///< used to determine, how to represent the kernel of BB^T in case of pure Dirichlet-BCs.
    double regularize_;                                         ///< If regularize_==0. no regularization is performed. Otherwise, a column is attached to Bs.
    template <typename ExT>
    void Update (const ExT& vel_ex, const ExT& p_ex) const;     ///< Updating the diagonal matrices D and Dprsqrtinv

  public:
    ISBBT_Stab_PreCL (const MatrixCL* B, const MatrixCL *C, const MatrixCL* M_pr, const MatrixCL* Mvel,
        const IdxDescCL& pr_idx, double kA= 0., double kM= 1., double tolA= 1e-2, double tolM= 1e-2, double regularize= 0., std::ostream* output= 0)
        : SchurPreBaseCL( kA, kM, output), B_( B), Bs_( 0), Bversion_( 0),
          M_( M_pr), Mvel_( Mvel), C_(C), tolA_(tolA), tolM_(tolM),
          PCsolver1_(), PCsolver2_(),
          solver_( PCsolver1_, 800, tolA_, /*relative*/ true),
          solver2_( PCsolver2_, 500, tolM_, /*relative*/ true),
          pr_idx_( &pr_idx), regularize_( regularize)
    {
    MminusC_.LinComb( 1.0, *M_, -1.0, *C_ );
    }

    ISBBT_Stab_PreCL (const ISBBT_Stab_PreCL& pc)
        : SchurPreBaseCL( pc.kA_, pc.kM_), B_( pc.B_), Bs_( pc.Bs_ == 0 ? 0 : new MatrixCL( *pc.Bs_)),
          Bversion_( pc.Bversion_),
          M_( pc.M_), Mvel_( pc.Mvel_), C_( pc.C_ ),
          PCsolver1_(), PCsolver2_(),
          solver_( PCsolver1_, 800, tolA_, /*relative*/ true),
          solver2_( PCsolver2_, 500, tolM_, /*relative*/ true),
          pr_idx_( pc.pr_idx_), regularize_( pc.regularize_)
    {
    MminusC_.LinComb( 1.0, *M_, -1.0, *C_ );
    }

    ISBBT_Stab_PreCL& operator= (const ISBBT_Stab_PreCL&) {
        throw DROPSErrCL( "ISBBT_Stab_PreCL::operator= is not permitted.\n");
    }

    ~ISBBT_Stab_PreCL () { delete Bs_; }

    template <typename Mat, typename Vec, typename ExT>
    void Apply(const Mat&, Vec& p, const Vec& c, const ExT& vel_ex, const ExT& p_ex) const;

#ifdef _PAR
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
#endif
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }

    using SchurPreBaseCL::Apply;

    void SetMatrices (const MatrixCL* B, const MatrixCL* C, const MatrixCL* Mvel, const MatrixCL* M, const IdxDescCL* pr_idx)
    {
        B_= B;
        C_= C;
        Mvel_= Mvel;
        M_= M;
        pr_idx_= pr_idx;
        Bversion_ = 0;
        MminusC_.LinComb( 1.0, *M_, -1.0, *C_ );
    }
};

template<typename ExT>
void ISBBT_Stab_PreCL::Update(const ExT& vel_ex, const ExT& p_ex) const
{
    std::cout << "ISBBT_Stab_PreCL::Update: old version: " << Bversion_
              << "\tnew version: " << B_->Version() << '\n';
    delete Bs_;
    Bs_= new MatrixCL( *B_);
    Bversion_= B_->Version();

    VectorCL Dvelinv( 1.0/ vel_ex.GetAccumulate(Mvel_->GetDiag()));
    ScaleCols( *Bs_, VectorCL( std::sqrt( Dvelinv)));

    MminusC_.LinComb( 1.0, *M_, -1.0, *C_ );
    VectorCL Dprsqrt( std::sqrt( p_ex.GetAccumulate( MminusC_.GetDiag())));
    Dprsqrtinv_.resize( MminusC_.num_rows());
    Dprsqrtinv_= 1.0/Dprsqrt;
    ScaleRows( *Bs_, Dprsqrtinv_);

#ifndef _PAR
    if (regularize_ != 0.)
        Regularize( *Bs_, *pr_idx_, Dprsqrt, PCsolver1_, regularize_, vel_ex, p_ex);
#endif
}

template <typename Mat, typename Vec, typename ExT>
void ISBBT_Stab_PreCL::Apply(const Mat&, Vec& p, const Vec& c, const ExT& vel_ex, const ExT& p_ex) const
{
    ScopeTimerCL scope("ISBBT_Stab_PreCL::Apply");
    if (B_->Version() != Bversion_)
        Update(vel_ex, p_ex);

    p= 0.0;
    if (kA_ != 0.0) {
        solver_.Solve( *Bs_, p, VectorCL( Dprsqrtinv_*c), vel_ex, p_ex);
        if (solver_.GetIter() == solver_.GetMaxIter()){
            std::cout << "ISBBT_Stab_PreCL::Apply: BBT-solve: " << solver_.GetIter()
                    << " (max)\t" << solver_.GetResid() << '\n';
        }
        else if (output_)
            *output_ << "ISBBT_Stab_PreCL BBT-solve: iterations: " << solver_.GetIter()
                     << "\tresidual: " <<  solver_.GetResid();
        p= kA_*(Dprsqrtinv_*p);
    }
    if (kM_ != 0.0) {
        Vec p2_( c.size() );
        solver2_.Solve( MminusC_, p2_, c, p_ex );
        if (solver2_.GetIter() == solver2_.GetMaxIter()){
            std::cout << "ISBBT_Stab_PreCL::Apply: (M-C)-solve: " << solver2_.GetIter()
                    << " (max)\t" << solver2_.GetResid() << '\n';
        }
        else if (output_)
            *output_ << "\tISBBT_Stab_PreCL (M-C)-solve: iterations: " << solver2_.GetIter()
                     << "\tresidual: " <<  solver2_.GetResid()
                     << '\n';

        p+= kM_*p2_;
    }
}

//**************************************************************************
// Preconditioner for the instationary (Navier-) Stokes-equations.
// It is a scaled version of the Min-Commutator-PC of Elman and can be used
// with P1X-elements.
//**************************************************************************

class MinCommPreCL : public SchurPreBaseCL
{
  private:
    const MatrixCL* A_, *B_, *Mvel_, *M_;
    mutable MatrixCL* Bs_;
    mutable size_t Aversion_, Bversion_, Mvelversion_, Mversion_;
    mutable VectorCL Dprsqrtinv_, Dvelsqrtinv_;
    double  tol_;

    #ifdef _PAR
    typedef ChebyshevBBTPcCL SPcT;                       ///< type of the preconditioner for solver
#else
    typedef NEGSPcCL SPcT;                               ///< type of the preconditioner for solver
#endif
    SPcT spc_;
    mutable PCGNESolverCL<SPcT> solver_;
    const IdxDescCL* pr_idx_;                            ///< Used to determine, how to represent the kernel of BB^T in case of pure Dirichlet-BCs.
    double regularize_;

    template <typename ExT>
    void Update (const ExT& vel_ex, const ExT& pr_ex) const;

  public:
    MinCommPreCL (const MatrixCL* A, MatrixCL* B, MatrixCL* Mvel, MatrixCL* M_pr, const IdxDescCL& pr_idx,
                  double tol=1e-2, double regularize= 0.0, std::ostream* output= 0)
        : SchurPreBaseCL( 0, 0, output), A_( A), B_( B), Mvel_( Mvel), M_( M_pr), Bs_( 0),
          Aversion_( 0), Bversion_( 0), Mvelversion_( 0), Mversion_( 0),
          tol_(tol),
          spc_( /*symmetric GS*/ true), solver_( spc_, 200, tol_, /*relative*/ true),
          pr_idx_( &pr_idx), regularize_( regularize) {}

    MinCommPreCL (const MinCommPreCL & pc)
        : SchurPreBaseCL( pc.kA_, pc.kM_), A_( pc.A_), B_( pc.B_), Mvel_( pc.Mvel_), M_( pc.M_),
          Bs_( pc.Bs_ == 0 ? 0 : new MatrixCL( *pc.Bs_)),
          Aversion_( pc.Aversion_), Bversion_( pc.Bversion_), Mvelversion_( pc.Mvelversion_),
          Mversion_( pc.Mversion_),
          Dprsqrtinv_( pc.Dprsqrtinv_), Dvelsqrtinv_( pc.Dvelsqrtinv_), tol_(pc.tol_),
          spc_( pc.spc_), solver_( spc_, 200, tol_, /*relative*/ true),
          pr_idx_( pc.pr_idx_), regularize_( pc.regularize_) {}

    MinCommPreCL& operator= (const MinCommPreCL&)     { throw DROPSErrCL( "MinCommPreCL::operator= is not permitted.\n"); }

    ~MinCommPreCL () { delete Bs_; }

    template <typename Mat, typename Vec, typename ExT>
    void Apply(const Mat&, Vec& p, const Vec& c, const ExT& vel_ex, const ExT& p_ex) const;
#ifdef _PAR
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
#endif
    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }

    using SchurPreBaseCL::Apply;

    void SetMatrixA  (const MatrixCL* A) { A_= A; }
    void SetMatrices (const MatrixCL* A, const MatrixCL* B, const MatrixCL* Mvel, const MatrixCL* M, const IdxDescCL* pr_idx)
    {
        A_= A;
        B_= B;
        Mvel_= Mvel;
        M_= M;
        pr_idx_= pr_idx;
        Aversion_ = Bversion_ = Mvelversion_ = Mversion_ = 0;
    }
};

template <typename Mat, typename Vec, typename ExT>
void MinCommPreCL::Apply (const Mat&, Vec& x, const Vec& b, const ExT& vel_ex, const ExT& pr_ex) const
{
    ScopeTimerCL scope("MinCommPreCL::Apply");
    if ((A_->Version() != Aversion_) || (Mvel_->Version() != Mvelversion_) || (B_->Version() != Bversion_))
        Update(vel_ex, pr_ex);

    VectorCL y( b.size());
    solver_.Solve( *Bs_, y, VectorCL( Dprsqrtinv_*b), vel_ex, pr_ex);
    if (solver_.GetIter() == solver_.GetMaxIter())
        std::cout << "MinCommPreCL::Apply: 1st BBT-solve: " << solver_.GetIter()
                  << " (max)\t" << solver_.GetResid() << '\n';
    else if (output_)
        *output_  << "MinCommPreCL::Apply: 1st BBT-solve: " << solver_.GetIter()
                  << '\t' << solver_.GetResid() << '\n';
    y*= Dprsqrtinv_;
    VectorCL z( Dprsqrtinv_*((*B_)*vel_ex.GetAccumulate(VectorCL( Dvelsqrtinv_*Dvelsqrtinv_*
        ( (*A_)*vel_ex.GetAccumulate(VectorCL( Dvelsqrtinv_*Dvelsqrtinv_*transp_mul( *B_, y)) ))))));
    VectorCL t( b.size());
    solver_.Solve( *Bs_, t, z, vel_ex, pr_ex);
    if (solver_.GetIter() == solver_.GetMaxIter())
        std::cout << "MinCommPreCL::Apply: 2nd BBT-solve: " << solver_.GetIter()
                  << " (max)\t" << solver_.GetResid() << '\n';
    else if (output_)
        *output_  << "MinCommPreCL::Apply: 2nd BBT-solve: " << solver_.GetIter()
                  << '\t' << solver_.GetResid() << '\n';
    x= Dprsqrtinv_*t;
}

template <typename ExT>
void MinCommPreCL::Update(const ExT& vel_ex, const ExT& pr_ex) const
{
    std::cout << "MinCommPreCL::Update: old/new versions: " << Aversion_  << '/' << A_->Version()
        << '\t' << Bversion_ << '/' << B_->Version() << '\t' << Mversion_ << '/' << M_->Version()
        << '\t' << Mvelversion_ << '/' << Mvel_->Version() << '\n';
    delete Bs_;
    Bs_= new MatrixCL( *B_);
    Aversion_= A_->Version();
    Bversion_= B_->Version();
    Mversion_= M_->Version();
    Mvelversion_= Mvel_->Version();

    Assert( vel_ex.GetAccumulate(Mvel_->GetDiag()).min() > 0., "MinCommPreCL::Update: Mvel_->GetDiag().min() <= 0\n", DebugNumericC);
    VectorCL Dvelsqrt( sqrt(vel_ex.GetAccumulate(Mvel_->GetDiag())));
    Dvelsqrtinv_.resize( Mvel_->num_rows());
    Dvelsqrtinv_= 1.0/Dvelsqrt;
    ScaleCols( *Bs_, Dvelsqrtinv_);

    Assert( pr_ex.GetAccumulate( M_->GetDiag()).min() > 0., "MinCommPreCL::Update: M_->GetDiag().min() <= 0\n", DebugNumericC);
    VectorCL Dprsqrt( std::sqrt( pr_ex.GetAccumulate( M_->GetDiag())));
    Dprsqrtinv_.resize( M_->num_rows());
    Dprsqrtinv_= 1.0/Dprsqrt;
    ScaleRows( *Bs_, Dprsqrtinv_);

#ifndef _PAR
    if (regularize_ != 0.)
        Regularize( *Bs_, *pr_idx_, Dprsqrt, spc_, regularize_, vel_ex, pr_ex);
#endif
}


//**************************************************************************
// Preconditioner for the instationary (Navier-) Stokes-equations.
// It uses the approximate Schur complement B diag(L)^{-1} B^T
// with L the upper left block of the saddle point system, and can be used
// with P1X-elements.
//**************************************************************************
class BDinvBTPreCL: public SchurPreBaseCL
{
  private:
    const MatrixCL* L_, *B_, *Mvel_, *M_;
    mutable MatrixCL* Bs_;
    mutable size_t Lversion_, Bversion_, Mvelversion_, Mversion_;
    mutable VectorCL Dprsqrtinv_, Dvelinv_, DSchurinv_;
    double  tol_;
    mutable DiagPcCL diagVelPc_;
    typedef DiagPcCL SchurPcT;
    //typedef ChebyshevPcCL SchurPcT;
    mutable SchurPcT SchurPc_;
#ifdef _PAR
    typedef ApproximateSchurComplMatrixCL<DiagPcCL, MatrixCL, ExchangeCL> AppSchurComplMatrixT;
    mutable AppSchurComplMatrixT* BDinvBT_;
#endif
    typedef ApproximateSchurComplMatrixCL<DiagPcCL, MatrixCL, DummyExchangeCL> SerAppSchurComplMatrixT;
    mutable SerAppSchurComplMatrixT* SerBDinvBT_;

    mutable PCGSolverCL<SchurPcT> solver_;

    const IdxDescCL* pr_idx_;                                   ///< Used to determine, how to represent the kernel of BB^T in case of pure Dirichlet-BCs.
    double regularize_;
    bool lumped_;

#ifdef _PAR
    void Update (const ExchangeCL& vel_ex, const ExchangeCL& pr_ex) const;
#endif
    void Update (const DummyExchangeCL& vel_ex, const DummyExchangeCL& pr_ex) const;

  public:
    BDinvBTPreCL (const MatrixCL* L, MatrixCL* B, MatrixCL* M_vel, MatrixCL* M_pr, const IdxDescCL& pr_idx,
                  double tol=1e-2, double regularize= 0.0, std::ostream* output= 0)
        : SchurPreBaseCL( 0, 0, output), L_( L), B_( B), Mvel_( M_vel), M_( M_pr), Bs_( 0),
          Lversion_( 0), Bversion_( 0), Mvelversion_( 0), Mversion_( 0), tol_(tol),
          diagVelPc_(Dvelinv_), SchurPc_( DSchurinv_),
#ifdef _PAR
          BDinvBT_(0),
#endif
          SerBDinvBT_(0),
          solver_( SchurPc_, 200,  tol_, /*relative*/ true), pr_idx_( &pr_idx),
          regularize_( regularize), lumped_(false) {}

    BDinvBTPreCL (const BDinvBTPreCL & pc)
        : SchurPreBaseCL( pc.kA_, pc.kM_), L_( pc.L_), B_( pc.B_), Mvel_( pc.Mvel_), M_( pc.M_),
          Bs_( pc.Bs_ == 0 ? 0 : new MatrixCL( *pc.Bs_)),
          Lversion_( pc.Lversion_), Bversion_( pc.Bversion_), Mvelversion_( pc.Mvelversion_), Mversion_( pc.Mversion_),
          Dprsqrtinv_( pc.Dprsqrtinv_), Dvelinv_( pc.Dvelinv_), DSchurinv_( pc.DSchurinv_), tol_(pc.tol_),
          diagVelPc_( Dvelinv_), SchurPc_( DSchurinv_),
#ifdef _PAR
          BDinvBT_(0),
#endif
          SerBDinvBT_(0),
          solver_( SchurPc_, 200, tol_, /*relative*/ true), pr_idx_( pc.pr_idx_),
          regularize_( pc.regularize_), lumped_( pc.lumped_) {}

    BDinvBTPreCL& operator= (const BDinvBTPreCL&) {
        throw DROPSErrCL( "BDinvBTPreCL::operator= is not permitted.\n");
    }

    ~BDinvBTPreCL ();

#ifdef _PAR
    template <typename Mat, typename Vec>
    void Apply(const Mat&, Vec& p, const Vec& c, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const;

    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const ExchangeCL& vel_ex, const ExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
#endif
    template <typename Mat, typename Vec>
    void Apply(const Mat&, Vec& p, const Vec& c, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const;

    void Apply(const MatrixCL& A,   VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }
    void Apply(const MLMatrixCL& A, VectorCL& x, const VectorCL& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& p_ex) const { Apply<>( A, x, b, vel_ex, p_ex); }

    using SchurPreBaseCL::Apply;

    void SetMatrixA  (const MatrixCL* L) { L_= L; }
    void SetMatrices (const MatrixCL* L, const MatrixCL* B, const MatrixCL* M_vel, const MatrixCL* M_pr,
                      const IdxDescCL* pr_idx) {
        L_= L;
        B_= B;
        Mvel_= M_vel;
        M_= M_pr;
        pr_idx_= pr_idx;
        Lversion_ = Bversion_ = Mvelversion_= Mversion_ = 0;
    }
    /// Returns true, if the lumped diag of the velocity mass matrix is used, and false, if the diagonal of the velocity convection-diffusion-reaction matrix is considered.
    bool UsesMassLumping() const { return lumped_; }
    /// For lump==true, the lumped diag of the velocity mass matrix is used. Otherwise the diagonal of the velocity convection-diffusion-reaction matrix is considered.
    void SetMassLumping( bool lump) { lumped_= lump; }
    /// If lumping is switched on, the lumped diag of the velocity mass matrix is returned. Otherwise the diagonal of the velocity convection-diffusion-reaction matrix is returned.
    template <typename ExT>
    VectorCL GetVelDiag(const ExT& vel_ex, const ExT& pr_ex) const {
        if ((L_->Version() != Lversion_) || (Mvel_->Version() != Mvelversion_))
            Update(vel_ex, pr_ex);
        return VectorCL(1.0/Dvelinv_);
    }
};

#ifdef _PAR
template <typename Mat, typename Vec>
void BDinvBTPreCL::Apply (const Mat&, Vec& x, const Vec& b, const ExchangeCL& vel_ex, const ExchangeCL& pr_ex) const
{
    ScopeTimerCL scope("BDinvBTPreCL::Apply");
    if ((L_->Version() != Lversion_) || (Mvel_->Version() != Mvelversion_) || (M_->Version() != Mversion_) || (B_->Version() != Bversion_))
        Update( vel_ex, pr_ex);

    Vec y( b.size());
    solver_.Solve( *BDinvBT_, y, Vec( Dprsqrtinv_*b), pr_ex);
    if (solver_.GetIter() == solver_.GetMaxIter())
        std::cout << "BDinvBTPreCL::Apply: BLBT-solve: " << solver_.GetIter()
                  << '\t' << solver_.GetResid() << '\n';
    x= Dprsqrtinv_*y;
}
#endif

template <typename Mat, typename Vec>
void BDinvBTPreCL::Apply (const Mat&, Vec& x, const Vec& b, const DummyExchangeCL& vel_ex, const DummyExchangeCL& pr_ex) const
{
    ScopeTimerCL scope("BDinvBTPreCL::Apply");
    if ((L_->Version() != Lversion_) || (Mvel_->Version() != Mvelversion_) || (M_->Version() != Mversion_) || (B_->Version() != Bversion_))
        Update( vel_ex, pr_ex);

    Vec y( b.size());
    solver_.Solve( *SerBDinvBT_, y, Vec( Dprsqrtinv_*b), pr_ex);
    if (solver_.GetIter() == solver_.GetMaxIter())
        std::cout << "BDinvBTPreCL::Apply: BLBT-solve: " << solver_.GetIter()
                  << '\t' << solver_.GetResid() << '\n';
    x= Dprsqrtinv_*y;
}

/// Upper block-triangular preconditioning strategy in BlockPreCL
struct UpperBlockPreCL
{
    template <class PC1T, class PC2T, class Mat, class Vec, class ExT>
    static void Apply (const PC1T& pc1, const PC2T& pc2, const Mat& A, const Mat& B, Vec& v, Vec& p, const Vec& b, const Vec& c, const ExT& vel_ex, const ExT& pr_ex) {
        pc2.Apply( /*dummy*/ B, p, c, vel_ex, pr_ex);
        p*= -1.;
        Vec b2( b);
        if (!pc2.RetAcc())
            pr_ex.Accumulate(p);
        b2-= transp_mul( B, p);
        pc1.Apply( A, v, b2, vel_ex);
    }
};

/// Block-diagonal preconditioning strategy in BlockPreCL
struct DiagBlockPreCL
{
    template <class PC1T, class PC2T, class Mat, class Vec, class ExT>
    static void Apply (const PC1T& pc1, const PC2T& pc2, const Mat& A, const Mat& B, Vec& v, Vec& p, const Vec& b, const Vec& c, const ExT& vel_ex, const ExT& pr_ex) {
        pc1.Apply( A, v, b, vel_ex);
        if (!pc1.RetAcc())
            vel_ex.Accumulate(v);
        pc2.Apply( /*dummy*/ B, p, c, vel_ex, pr_ex);
        p*= -1.;
   }
};

/// Block-diagonal preconditioning strategy in BlockPreCL if an spd preconditioner is required
struct DiagSpdBlockPreCL
{
    template <class PC1T, class PC2T, class Mat, class Vec, class ExT>
    static void Apply (const PC1T& pc1, const PC2T& pc2, const Mat& A, const Mat& B, Vec& v, Vec& p, const Vec& b, const Vec& c, const ExT& vel_ex, const ExT& pr_ex) {
        pc1.Apply( A, v, b, vel_ex);
        if (!pc1.RetAcc())
            vel_ex.Accumulate(v);
        pc2.Apply( /*dummy*/ B, p, c, vel_ex, pr_ex);
   }
};

/// Lower block-triangular preconditioning strategy in BlockPreCL
struct LowerBlockPreCL
{
    template <class PC1T, class PC2T, class Mat, class Vec, class ExT>
    static void Apply (const PC1T& pc1, const PC2T& pc2, const Mat& A, const Mat& B, Vec& v, Vec& p, const Vec& b, const Vec& c, const ExT& vel_ex, const ExT& pr_ex) {
        pc1.Apply( A, v, b, vel_ex);
        if (!pc1.RetAcc())
            vel_ex.Accumulate(v);
        Vec c2( B*v - c);
        pc2.Apply( /*dummy*/ B, p, c2, vel_ex, pr_ex);
    }
};

/// SIMPLER type preconditioning strategy in BlockPreCL
struct SIMPLERBlockPreCL
{
    template <class PC1T, class PC2T, class Mat, class Vec, class ExT>
    static void Apply (PC1T& pc1, PC2T& pc2, const Mat& A, const Mat& B, Vec& v, Vec& p, const Vec& b, const Vec& c, const ExT& vel_ex, const ExT& pr_ex) {
        Vec D( pc2.GetVelDiag(vel_ex, pr_ex)), dp(p);

        // find some initial pressure for the SIMPLER preconditioner (initial p=0 would result in SIMPLE)
        pc2.Apply( B, p, Vec(B*Vec(vel_ex.GetAccumulate(b)/D) - c), vel_ex, pr_ex);
        if (!pc2.RetAcc())
            pr_ex.Accumulate(p);

        // from here on it's the SIMPLE preconditioner
        pc1.Apply( A, v, Vec(b - transp_mul( B, p)), vel_ex);
        if (!pc1.RetAcc()) vel_ex.Accumulate(v);

        pc2.Apply( /*dummy*/ B, dp, Vec(B*v - c), vel_ex, pr_ex);

        if (!pc2.RetAcc()) pr_ex.Accumulate(dp);
        v-= vel_ex.GetAccumulate(Vec(transp_mul( B, dp)/D));
        p+= dp;
   }
};

// With BlockShapeT= DiagBlockPreCL, this is the diagonal PC ( pc1^(-1) 0 \\ 0 pc2^(-1) ),
// else it is block-triangular:
// Upper... ( pc1^(-1) B^T \\ 0 pc2^(-1) ), resp. lower: ( pc1^(-1) 0 \\ B pc2^(-1) )
template <class PC1T, class PC2T, class BlockShapeT= DiagBlockPreCL>
class BlockPreCL
{
  private:
    PC1T& pc1_; // Preconditioner for A.
    PC2T& pc2_; // Preconditioner for S.

  public:
    BlockPreCL (PC1T& pc1, PC2T& pc2)
        : pc1_( pc1), pc2_( pc2) {}

    template <typename Mat, typename Vec, typename ExT>
    void Apply(const Mat& A, const Mat& B, Vec& v, Vec& p, const Vec& b, const Vec& c, const ExT& vel_ex, const ExT& pr_ex) const {
        BlockShapeT::Apply( pc1_, pc2_, A, B, v, p, b, c, vel_ex, pr_ex);
    }

    template <typename Mat, typename Vec, typename ExT>
    void Apply(const BlockMatrixBaseCL<Mat>& A, Vec& x, const Vec& b, const ExT& ex) const {
        VectorCL b0( b[std::slice( 0, A.num_rows( 0), 1)]);
        VectorCL b1( b[std::slice( A.num_rows( 0), A.num_rows( 1), 1)]);
        VectorCL x0( A.num_cols( 0));
        VectorCL x1( A.num_cols( 1));
        BlockShapeT::Apply( pc1_, pc2_, *A.GetBlock( 0), *A.GetBlock( 2), x0, x1, b0, b1, ex.GetEx(0), ex.GetEx(1));
        x[std::slice( 0, A.num_cols( 0), 1)]= x0;
        x[std::slice( A.num_cols( 0), A.num_cols( 1), 1)]= x1;
    }

    /// \brief Check if the preconditioned vector is accumulated
    bool RetAcc() const {
        Assert( pc1_.RetAcc()==pc2_.RetAcc(), DROPSErrCL("BlockPreCL::RetAcc: Preconditioners do not match"),
                DebugNumericC);
        return pc1_.RetAcc();
    }

    /// \brief Check if the diagonal of the matrix needs to be computed
    bool NeedDiag() const { return pc1_.NeedDiag() || pc2_.NeedDiag(); }

    /// \brief Set accumulated diagonal of a matrix, that is needed by most of the preconditioners
    template<typename Mat, typename ExT>
    void SetDiag(const Mat& A, const ExT& ex) const
    {
        pc1_.SetDiag(*A.GetBlock( 0), ex.GetEx(0));
        pc2_.SetDiag(/*dummy*/ *(A.GetBlock( 3)!=0 ? A.GetBlock( 3) : A.GetBlock( 1)), A.GetBlock( 3)!=0 ? ex.GetEx( 1) : ex.GetEx( 0));
    }
    const PC1T& GetPC1() const { return pc1_; }
          PC1T& GetPC1()       { return pc1_; }
    const PC2T& GetPC2() const { return pc2_; }
          PC2T& GetPC2()       { return pc2_; }

};

} // end of namespace DROPS

#endif /* OSEENPRECOND_H_ */

/// \file instatstokes2phase.cpp
/// \brief classes that constitute the 2-phase Stokes problem
/// \author LNM RWTH Aachen: Jens Berger, Patrick Esser, Joerg Grande, Sven Gross, Volker Reichelt, Liang Zhang, Yuanjun Zhang; SC RWTH Aachen: Oliver Fortmeier

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
 * Copyright 2009 LNM/SC RWTH Aachen, Germany
*/

#include "stokes/instatstokes2phase.h"
#include "num/accumulator.h"
#include "num/quadrature.h"
#include "num/lattice-eval.h"
#include "misc/progressaccu.h"
#include "misc/scopetimer.h"

#include <set>

//extern DROPS::ParamCL P;

namespace DROPS
{
// -----------------------------------------------------------------------------
//                        Routines for SetupSystem2
// -----------------------------------------------------------------------------

class EllipsoidCL;
void SetupSystem2_P2P0( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL&, const StokesBndDataCL& BndData,
                        MatrixCL* B, VecDescCL* c, IdxDescCL* RowIdx, IdxDescCL* ColIdx, double t)
// P2 / P0 FEs for vel/pr
{
    ScopeTimerCL scope("SetupSystem2_P2P0");

    SparseMatBuilderCL<double, SMatrixCL<1,3> >  mB( B, RowIdx->NumUnknowns(), ColIdx->NumUnknowns());
    if (c != 0) c->Clear( t);
    const Uint lvl= RowIdx->TriangLevel();
    LocalNumbP2CL n;
    Quad2CL<Point3DCL> Grad[10], GradRef[10];
    SMatrixCL<3,3> T;
    double det, absdet;
    Point3DCL tmp;

    const Uint pidx= RowIdx->GetIdx();

    P2DiscCL::GetGradientsOnRef( GradRef);
    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit) {
        GetTrafoTr( T, det, *sit);
        P2DiscCL::GetGradients( Grad, GradRef, T);
        absdet= std::fabs( det);
        n.assign( *sit, *ColIdx, BndData.Vel);
        const IdxT prNumbTetra= sit->Unknowns(pidx);

        // Setup B:   b(i,j) =  -\int psi_i * div( phi_j)
        for(int vel=0; vel<10; ++vel) {
            if (n.WithUnknowns( vel))
            {
                tmp= Grad[vel].quad( absdet);
                mB( prNumbTetra, n.num[vel])  -=  SMatrixCL<1,3>(tmp);
            }
            else if (c != 0) // put coupling on rhs
            {
                typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
                bnd_val_fun bf= BndData.Vel.GetBndSeg( n.bndnum[vel]).GetBndFun();
                tmp= vel<4 ? bf( sit->GetVertex( vel)->GetCoord(), t)
                        : bf( GetBaryCenter( *sit->GetEdge( vel-4)), t);
                c->Data[ prNumbTetra]+= inner_prod( Grad[vel].quad( absdet), tmp);
            }
        }
    }
    mB.Build();
}


void SetupSystem2_P2P1( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL& coeff, const StokesBndDataCL& BndData, MatrixCL* B, VecDescCL* c,
        IdxDescCL* RowIdx, IdxDescCL* ColIdx, double t)
/// Set up matrices B and rhs c
{
    ScopeTimerCL scope("SetupSystem2_P2P1");
    System2Accumulator_P2P1CL<TwoPhaseFlowCoeffCL> accu( coeff, BndData, *RowIdx, *ColIdx, *B, c, t);
    TetraAccumulatorTupleCL accus;
    MaybeAddProgressBar(MG, "System2(P2P1) Setup", accus, RowIdx->TriangLevel());
    accus.push_back( &accu);
    accumulate( accus, MG, RowIdx->TriangLevel(), RowIdx->GetMatchingFunction(), RowIdx->GetBndInfo());
}


//P2_P1X multiplication
void SpecialBndHandleSystem2_P2P1XCL::setupB(SMatrixCL<1, 3> loc_b[4][10], const TetraCL& tet, int ls_sign[4], const PrincipalLatticeCL& lat, 
    const std::valarray<double>& ls_loc_, IdxT prNumb[4], const ExtIdxDescCL* Xidx_)
{
	GridFunctionCL<> qvel[6];
	GridFunctionCL<> qpr[3];
	//int sign1=0,sign2=0;
	for (Uint k =0; k< 4; ++k) //Go throught all faces of a tet
	{
		if( BndData_.Vel.GetBC(*tet.GetFace(k))==Slip0BC || BndData_.Vel.GetBC(*tet.GetFace(k))==SlipBC || BndData_.Vel.GetBC(*tet.GetFace(k))==SymmBC)
		{
			//sign1=0,sign2=0;
			//for(Uint j=0; j<3; j++)
			//{
			//	sign1= (ls_sign[j<k?j:j+1]<0)?-1:sign1;
			//	sign2= (ls_sign[j<k?j:j+1]>0)?1:sign2;
			//}
		//	std::cout<<sign1<<" "<<sign2<<std::endl;
		//	if(sign1*sign2!=-1)	continue;

		    tet.GetOuterNormal(k, normal);

			bndpartition_.make_partition2D<SortedVertexPolicyCL, MergeCutPolicyCL>( lat, k, ls_loc_);
			make_CompositeQuad5BndDomain2D( bndq5dom_, bndpartition_,tet);

			LocalP2CL<double> phiVelP2[6];   //local basis for velocity
			LocalP1CL<double> phiPrP1[3];    //local basis for pressure

			for (Uint i= 0; i<3; ++i)
			{
				unknownIdx[i]   = VertOfFace(k, i);          // i is index for Vertex
				unknownIdx[i+3] = EdgeOfFace(k, i) + 4;      // i is index for Edge
				phiPrP1[i][unknownIdx[i]]=1;
				resize_and_evaluate_on_vertexes(phiPrP1[i], bndq5dom_, qpr[i]);
			}
		    
			for(Uint j=0; j<3; ++j){
				const IdxT xidx= (*Xidx_)[prNumb[ unknownIdx[j] ] ];
				if (xidx==NoIdx) continue;
				const bool is_pos= ls_sign[unknownIdx[j]] == 1;
				for(Uint i=0; i<6; ++i){
					phiVelP2[i][unknownIdx[i]] = 1;
					resize_and_evaluate_on_vertexes(phiVelP2[i], bndq5dom_, qvel[i]);
					const double value=(is_pos ? -1. : 1.)*quad( qvel[i]*qpr[j], bndq5dom_, is_pos ? NegTetraC : PosTetraC);
				    loc_b[unknownIdx[j]][unknownIdx[i]](0, 0)-= value*normal[0]; //need verification!
					loc_b[unknownIdx[j]][unknownIdx[i]](0, 1)-= value*normal[1];
					loc_b[unknownIdx[j]][unknownIdx[i]](0, 2)-= value*normal[2];
				}
			}
		}
	}
}


/// \brief Accumulator to set up the matrix B and, if requested the right-hand side C for two-phase flow.
class System2Accumulator_P2P1XCL : public System2Accumulator_P2P1CL<TwoPhaseFlowCoeffCL>
{
  private:
    typedef System2Accumulator_P2P1CL<TwoPhaseFlowCoeffCL> base_;
    using base_::coeff;
    using base_::BndData;
    const LevelsetP2CL&        lset_;

    using base_::lat;

    using base_::t;

    using base_::prNumb;  ///< global numbering of the P1-unknowns
    using base_::n;          ///< global numbering of the P2-unknowns

    using base_::dirichlet_val; ///< Dirichlet values, filled in only on the Dirichlet-boundary.

    using base_::mB_;
    using base_::c;

    using base_::T;
    using base_::absdet;

    LocalP2CL<double>         local_p2_lset;
    std::valarray<double>     ls_loc_;
    int                       ls_sign_[4];
    TetraPartitionCL          partition_;
    QuadDomainCL              q2dom_;
    GridFunctionCL<Point3DCL> qgrad_[10];
    LocalP1CL<Point3DCL>      GradRefLP1_[10],
                              GradLP1_[10];

    SMatrixCL<1,3>            loc_B_[4][10]; ///< transposed representation for better memory-access.

    bool 	spebnd;
    SpecialBndHandleSystem2_P2P1XCL speBndHandle;	///< Slip or symmetric boundary condition handler



    using base_::RowIdx;
    const ExtIdxDescCL* Xidx_;

    ///\brief Computes the mapping from local to global data "n", the local matrices in loc and, if required, the Dirichlet-values needed to eliminate the boundary-dof from the global system.
    void local_setup (const TetraCL& tet);
    ///\brief Update the global system.
    void update_global_system ();

  public:
    System2Accumulator_P2P1XCL ( const TwoPhaseFlowCoeffCL& coeff_arg, const StokesBndDataCL& BndData_arg,
        const LevelsetP2CL& lset, const IdxDescCL& RowIdx_arg, const IdxDescCL& ColIdx_arg,
        MatrixCL& B_arg, VecDescCL* c_arg, double t_arg);

    ///\brief Initializes matrix-builders and load-vectors
    void begin_accumulation ();
    ///\brief Builds the matrices
    void finalize_accumulation();

    void visit (const TetraCL& tet);

    TetraAccumulatorCL* clone (int /*tid*/){ return new System2Accumulator_P2P1XCL ( *this); };
};

System2Accumulator_P2P1XCL::System2Accumulator_P2P1XCL (const TwoPhaseFlowCoeffCL& coeff_arg, const StokesBndDataCL& BndData_arg,
        const LevelsetP2CL& lset, const IdxDescCL& RowIdx_arg, const IdxDescCL& ColIdx_arg,
        MatrixCL& B_arg, VecDescCL* c_arg, double t_arg)
    :  base_( coeff_arg, BndData_arg, RowIdx_arg, ColIdx_arg, B_arg, c_arg, t_arg), lset_( lset), ls_loc_( lat.vertex_size()), speBndHandle(BndData_arg)
{
    P2DiscCL::GetGradientsOnRef( GradRefLP1_);
}

void System2Accumulator_P2P1XCL::begin_accumulation ()
{
    base_::begin_accumulation();
    Xidx_ = &RowIdx.GetXidx();
}

void System2Accumulator_P2P1XCL::finalize_accumulation ()
{
    base_::finalize_accumulation();
}

void System2Accumulator_P2P1XCL::visit (const TetraCL& tet)
{
    base_::visit( tet);
    evaluate_on_vertexes( lset_.GetSolution(), tet, lat, Addr( ls_loc_));
    if (equal_signs( ls_loc_)) return; // extended basis functions have only support on tetra intersecting Gamma.

    partition_.make_partition<SortedVertexPolicyCL, MergeCutPolicyCL>( lat, ls_loc_);
    make_CompositeQuad2Domain( q2dom_, partition_);
    local_p2_lset.assign(tet, *lset_.PhiC, lset_.GetBndData());

    local_setup(tet);
    update_global_system();
}

void System2Accumulator_P2P1XCL::local_setup (const TetraCL& tet)
{
    P2DiscCL::GetGradients( GradLP1_, GradRefLP1_, T);
    for (int i= 0; i < 10; ++i) // Gradients of the velocity hat-functions
        resize_and_evaluate_on_vertexes(  GradLP1_[i], q2dom_, qgrad_[i]);

    for (int i= 0; i < 4; ++i) // sign of the level-set function in the vertices
        ls_sign_[i]= sign(local_p2_lset[i]);

    GridFunctionCL<> qpr;
    LocalP1CL<> p1;
    Point3DCL B_entry;
    for(int pr=0; pr<4; ++pr) {
        // p1 is the P1 hat-function for the dof pr.
        p1[pr]= 1.; p1[pr==0 ? 3 : pr - 1]= 0.;
        // compute the integrals I = \int_{T_+} grad v_vel p_pr dx  -  C \int_{T}grad v_vel p_pr dx,
        // where C= (sign Phi_pr==1) \in {0,1} and T_+ = T \cap \Omega_2 (positive part)
        const IdxT xidx= (*Xidx_)[prNumb[pr]];
        if (xidx==NoIdx) continue;

        resize_and_evaluate_on_vertexes( p1, q2dom_, qpr);
        for(int vel=0; vel<10; ++vel) {
            const bool is_pos= ls_sign_[pr] == 1;
            // for C=0 (<=> !is_pos) we have I = -\int_{T_-} grad v_vel p_pr dx
            // for C=1 (<=>  is_pos) we have I =  \int_{T_+} grad v_vel p_pr dx
            loc_B_[pr][vel]= SMatrixCL<1,3>( (is_pos ? -1. : 1.)*quad( qgrad_[vel]*qpr, absdet, q2dom_, is_pos ? NegTetraC : PosTetraC));
        }
    }


	//if there is at least one slip or symmetric boundary on this tetra
    spebnd=false;
	for(int i =0; i< 4; ++i){
		if(BndData.Vel.GetBC(*tet.GetFace(i))==Slip0BC  || BndData.Vel.GetBC(*tet.GetFace(i))==SlipBC || BndData.Vel.GetBC(*tet.GetFace(i))==SymmBC)
		{
			spebnd=true;
			break;
		}
	}
	if(spebnd)
		speBndHandle.setupB(loc_B_,tet,ls_sign_,lat,ls_loc_, prNumb, Xidx_);
}

void System2Accumulator_P2P1XCL::update_global_system ()
{
    SparseMatBuilderCL<double, SMatrixCL<1,3> >& mB= (*mB_);
    for(int pr=0; pr<4; ++pr) {
        const IdxT xidx= (*Xidx_)[prNumb[pr]];
        if (xidx==NoIdx) continue;

        for(int vel=0; vel<10; ++vel) {
            if (n.WithUnknowns( vel)) {
                mB( xidx, n.num[vel])-= loc_B_[pr][vel];
            }
            else if (c != 0)
                c->Data[ xidx]+= inner_prod( loc_B_[pr][vel], dirichlet_val[vel]);
        }
    }
}

void SetupSystem2_P2P1X( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL& coeff, const StokesBndDataCL& BndData, MatrixCL* B, VecDescCL* c, const LevelsetP2CL& lset, IdxDescCL* RowIdx, IdxDescCL* ColIdx, double t)
// P2 / P1X FEs (X=extended) for vel/pr
{
    ScopeTimerCL scope("SetupSystem2_P2P1X");
    System2Accumulator_P2P1XCL p1x_accu( coeff, BndData, lset, *RowIdx, *ColIdx, *B, c, t);
    TetraAccumulatorTupleCL accus;
    MaybeAddProgressBar(MG, "System2(P2P1X) Setup", accus, RowIdx->TriangLevel());
    accus.push_back( &p1x_accu);
    accumulate( accus, MG, RowIdx->TriangLevel(), RowIdx->GetMatchingFunction(), RowIdx->GetBndInfo());
}

inline void ComputePgradV( LocalP2CL<Point3DCL>& PgradV, Uint pr, const Quad2CL<Point3DCL>& gradV)
{
    PgradV= Point3DCL();
    PgradV[pr]= gradV[pr];
    for (Uint vert=0; vert<4; ++vert)
        if (vert!=pr)
            PgradV[EdgeByVert(pr,vert)+4]= 0.25*(gradV[pr]+gradV[vert]);
}


void SetupSystem2_P2RP1X( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL&, const StokesBndDataCL& BndData, MatrixCL* B, VecDescCL* c, const LevelsetP2CL& lset, IdxDescCL* RowIdx, IdxDescCL* ColIdx, double t)
// P2X / P1X FEs (X=extended) for vel/pr
{
    ScopeTimerCL scope("SetupSystem2_P2RP1X");

    const ExtIdxDescCL& prXidx=    RowIdx->GetXidx();
    const ExtIdxDescCL& velXidx= ColIdx->GetXidx();
    SparseMatBuilderCL<double, SMatrixCL<1,3> >  mB( B, RowIdx->NumUnknowns(), ColIdx->NumUnknowns());
    if (c != 0) c->Clear( t);
    const Uint lvl= RowIdx->TriangLevel();
    IdxT prNumb[4];
    LocalNumbP2CL n;
    Quad2CL<Point3DCL> Grad[10], GradRef[10], gradVx;
    LocalP2CL<> velR_p[4][8], velR_n[4][8];
    SMatrixCL<3,3> T;
    double det, absdet;
    Point3DCL tmp;
    InterfaceTetraCL cut;
    LocalP2CL<> loc_phi;

    P2DiscCL::GetGradientsOnRef( GradRef);
    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit) {
        GetTrafoTr( T, det, *sit);
        P2DiscCL::GetGradients( Grad, GradRef, T);
        absdet= std::fabs( det);
        n.assign( *sit, *ColIdx, BndData.Vel);
        GetLocalNumbP1NoBnd( prNumb, *sit, *RowIdx);
        // Setup B:   b(i,j) =  -\int psi_i * div( phi_j)

        // do setup for std FEM part
        for(int vel=0; vel<10; ++vel) {
            if (n.WithUnknowns( vel))
                for(int pr=0; pr<4; ++pr) {
                    tmp= Grad[vel].quadP1( pr, absdet);
                    mB( prNumb[pr], n.num[vel])  -=  SMatrixCL<1,3>(tmp);
                }
            else if (c != 0) { // put coupling on rhs
                typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
                bnd_val_fun bf= BndData.Vel.GetBndSeg( n.bndnum[vel]).GetBndFun();
                tmp= vel<4 ? bf( sit->GetVertex( vel)->GetCoord(), t)
                        : bf( GetBaryCenter( *sit->GetEdge( vel-4)), t);
                for(int pr=0; pr<4; ++pr)
                    c->Data[ prNumb[pr]]+= inner_prod( Grad[vel].quadP1( pr, absdet), tmp);
            }
        }
        // now handle extended dofs
        loc_phi.assign( *sit, *lset.PhiC, lset.GetBndData());
        cut.Init( *sit, loc_phi);
        if (!cut.Intersects()) continue; // extended basis functions have only support on tetra intersecting Gamma!

        P2RidgeDiscCL::GetExtBasisOnChildren( velR_p, velR_n, loc_phi);
        for(int pr=0; pr<4; ++pr) {
            // compute the integrals
            // I = \int_{T_+} grad v_vel p_pr dx  -  C \int_{T}grad v_vel p_pr dx,
            // where C= (sign Phi_pr==1) \in {0,1} and T_+ = T \cap \Omega_2 (positive part)
            LocalP2CL<Point3DCL> PgradV;
            const IdxT xidx= prXidx[prNumb[pr]];

            for(int vel= (xidx!=NoIdx ? 0 : 10); vel<14; ++vel)
            {
                const IdxT stdvidx= vel<10 ? n.num[vel] : n.num[vel-10],
                        xvidx= (vel>=10 && stdvidx!=NoIdx) ? velXidx[stdvidx] : NoIdx,
                        vidx= vel<10 ? stdvidx : xvidx;
                if (vel>=10 && xvidx==NoIdx) continue; // no extended vel dof

                Point3DCL int_Px_gradV, int_P_gradV;
                const bool is_pos= cut.GetSign(pr)==1;

                if (vel<10) { // standard P2 FE
                    ComputePgradV( PgradV, pr, Grad[vel]);
                    for (int ch=0; ch<8; ++ch)
                    {
                        cut.ComputeCutForChild(ch);
                        int_Px_gradV+= cut.quad( PgradV, absdet, !is_pos); // integrate on other part
                    }
                    // for C=1 (<=> is_pos) we have I = -\int_{T_-} grad v_vel p_pr dx
                    if (is_pos) int_Px_gradV= -int_Px_gradV;
                } else { // ridge enrichment for vel
                    Point3DCL intNeg, intPos;
                    for (int ch=0; ch<8; ++ch)
                    {
                        cut.ComputeCutForChild(ch);
                        // integrate on pos. part
                        P2DiscCL::GetFuncGradient( gradVx, velR_p[vel-10][ch], Grad);
                        ComputePgradV( PgradV, pr, gradVx);
                        intPos+= cut.quad( PgradV, absdet, true);
                        // integrate on neg. part
                        P2DiscCL::GetFuncGradient( gradVx, velR_n[vel-10][ch], Grad);
                        ComputePgradV( PgradV, pr, gradVx);
                        intNeg+= cut.quad( PgradV, absdet, false);
                    }
                    int_P_gradV= intPos + intNeg;
                    // for C=1 (<=> is_pos) we have I = -\int_{T_-} grad v_vel p_pr dx
                    int_Px_gradV= is_pos ? -intNeg : intPos;
                }

                if (vidx!=NoIdx)
                {
                    if (xidx!=NoIdx) {
                        mB( xidx, vidx)  -=  SMatrixCL<1,3>(int_Px_gradV);
                    }
                    if (vel>=10) { // extended vel: write also entry for p_gradVx
                        const IdxT pidx= prNumb[pr];
                        mB( pidx, vidx)  -=  SMatrixCL<1,3>(int_P_gradV);
                    }
                }
                else if (c != 0 && vel<10)
                { // put coupling on rhs
                    typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
                    bnd_val_fun bf= BndData.Vel.GetBndSeg( n.bndnum[vel]).GetBndFun();
                    tmp= vel<4 ? bf( sit->GetVertex( vel)->GetCoord(), t)
                            : bf( GetBaryCenter( *sit->GetEdge( vel-4)), t);
                    c->Data[ xidx]+= inner_prod( int_Px_gradV, tmp);
                }
            }
        }
    }
    mB.Build();
}


void SetupSystem2_P2RP1( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL&, const StokesBndDataCL& BndData, MatrixCL* B, VecDescCL* c, const LevelsetP2CL& lset, IdxDescCL* RowIdx, IdxDescCL* ColIdx, double t)
// P2X / P1 FEs (X=extended) for vel/pr
{
    ScopeTimerCL scope("SetupSystem2_P2RP1");

    const ExtIdxDescCL& velXidx= ColIdx->GetXidx();
    SparseMatBuilderCL<double, SMatrixCL<1,3> > mB( B, RowIdx->NumUnknowns(), ColIdx->NumUnknowns());
    if (c != 0) c->Clear( t);
    const Uint lvl= RowIdx->TriangLevel();
    IdxT prNumb[4];
    LocalNumbP2CL n;
    Quad2CL<Point3DCL> Grad[10], GradRef[10], gradVx;
    LocalP2CL<> velR_p[4][8], velR_n[4][8];
    SMatrixCL<3,3> T;
    double det, absdet;
    Point3DCL tmp;
    InterfaceTetraCL cut;
    LocalP2CL<> loc_phi;

    P2DiscCL::GetGradientsOnRef( GradRef);
    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit) {
        GetTrafoTr( T, det, *sit);
        P2DiscCL::GetGradients( Grad, GradRef, T);
        absdet= std::fabs( det);
        n.assign( *sit, *ColIdx, BndData.Vel);
        GetLocalNumbP1NoBnd( prNumb, *sit, *RowIdx);
        // Setup B:   b(i,j) =  -\int psi_i * div( phi_j)

        // do setup for std FEM part
        for(int vel=0; vel<10; ++vel) {
            if (n.WithUnknowns( vel))
                for(int pr=0; pr<4; ++pr) {
                    tmp= Grad[vel].quadP1( pr, absdet);
                    mB( prNumb[pr], n.num[vel])  -=  SMatrixCL<1,3>(tmp);
                }
            else if (c != 0) { // put coupling on rhs
                typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
                bnd_val_fun bf= BndData.Vel.GetBndSeg( n.bndnum[vel]).GetBndFun();
                tmp= vel<4 ? bf( sit->GetVertex( vel)->GetCoord(), t)
                        : bf( GetBaryCenter( *sit->GetEdge( vel-4)), t);
                for(int pr=0; pr<4; ++pr)
                    c->Data[ prNumb[pr]]+= inner_prod( Grad[vel].quadP1( pr, absdet), tmp);
            }
        }
        // now handle extended dofs
        loc_phi.assign( *sit, *lset.PhiC, lset.GetBndData());
        cut.Init( *sit, loc_phi);
        if (!cut.Intersects()) continue; // extended basis functions have only support on tetra intersecting Gamma!

        P2RidgeDiscCL::GetExtBasisOnChildren( velR_p, velR_n, loc_phi);
        for(int pr=0; pr<4; ++pr) {
            // compute the integrals
            // I = \int_{T_+} grad v_vel p_pr dx  -  C \int_{T}grad v_vel p_pr dx,
            // where C= (sign Phi_pr==1) \in {0,1} and T_+ = T \cap \Omega_2 (positive part)
            LocalP2CL<Point3DCL> PgradV;

            for(int xvel=0; xvel<4; ++xvel)
            {
                const IdxT stdvidx= n.num[xvel],
                    xvidx= stdvidx!=NoIdx ? velXidx[stdvidx] : NoIdx;
                if (xvidx==NoIdx) continue; // no extended vel dof

                Point3DCL int_P_gradV;

                Point3DCL intNeg, intPos;
                for (int ch=0; ch<8; ++ch)
                {
                    cut.ComputeCutForChild(ch);
                    // integrate on pos. part
                    P2DiscCL::GetFuncGradient( gradVx, velR_p[xvel][ch], Grad);
                    ComputePgradV( PgradV, pr, gradVx);
                    intPos+= cut.quad( PgradV, absdet, true);
                    // integrate on neg. part
                    P2DiscCL::GetFuncGradient( gradVx, velR_n[xvel][ch], Grad);
                    ComputePgradV( PgradV, pr, gradVx);
                    intNeg+= cut.quad( PgradV, absdet, false);
                }
                int_P_gradV= intPos + intNeg;

                // extended vel: write entry for p_gradVx
                const IdxT pidx= prNumb[pr];
                mB( pidx, xvidx)  -=  SMatrixCL<1,3>(int_P_gradV);
            }
        }
    }
    mB.Build();
}


void SetupSystem2_P2P1D( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL&, const StokesBndDataCL& BndData, MatrixCL* B, VecDescCL* c,
                         IdxDescCL* RowIdx, IdxDescCL* ColIdx, double t)
// P2 / P1D FEs for vel/pr
{
    ScopeTimerCL scope("SetupSystem2_P2P1D");

    SparseMatBuilderCL<double, SMatrixCL<1,3> > mB( B, RowIdx->NumUnknowns(), ColIdx->NumUnknowns());
    if (c != 0) c->Clear( t);
    const Uint lvl= RowIdx->TriangLevel();
    IdxT prNumb[4];
    LocalNumbP2CL n;
    Quad2CL<Point3DCL> Grad[10], GradRef[10];
    SMatrixCL<3,3> T;
    double det, absdet;
    Point3DCL tmp;

    P2DiscCL::GetGradientsOnRef( GradRef);
    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
        send=MG.GetTriangTetraEnd( lvl); sit != send; ++sit) {
        GetTrafoTr( T, det, *sit);
        P2DiscCL::GetGradients( Grad, GradRef, T);
        absdet= std::fabs( det);
        n.assign( *sit, *ColIdx, BndData.Vel);
        GetLocalNumbP1DNoBnd( prNumb, *sit, *RowIdx);
        // Setup B:   b(i,j) =  -\int psi_i * div( phi_j)
        for(int vel=0; vel<10; ++vel) {
            if (n.WithUnknowns( vel))
                for(int pr=0; pr<4; ++pr) {
                    tmp= Grad[vel].quadP1D( pr, absdet);
                    mB( prNumb[pr], n.num[vel])  -=  SMatrixCL<1,3>(tmp);
                }
            else if (c != 0)
            { // put coupling on rhs
                typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
                bnd_val_fun bf= BndData.Vel.GetBndSeg( n.bndnum[vel]).GetBndFun();
                tmp= vel<4 ? bf( sit->GetVertex( vel)->GetCoord(), t)
                        : bf( GetBaryCenter( *sit->GetEdge( vel-4)), t);
                for(int pr=0; pr<4; ++pr)
                    c->Data[ prNumb[pr]]+= inner_prod( Grad[vel].quadP1D( pr, absdet), tmp);
            }
        }
    }
    mB.Build();
}


// -----------------------------------------------------------------------------
//                        Routines for SetupRhs2
// -----------------------------------------------------------------------------


void SetupRhs2_P2P0( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL&, const StokesBndDataCL& BndData, VecDescCL* c, double t)
{
    ScopeTimerCL scope("SetupSystem2_P2P0");

    c->Clear( t);
    const Uint lvl= c->GetLevel();
    const Uint pidx= c->RowIdx->GetIdx();

    bool IsOnDirBnd[10];
    Quad2CL<Point3DCL> Grad_vel, GradRef[10];
    SMatrixCL<3,3> T;
    double det, absdet;
    Point3DCL tmp;

    P2DiscCL::GetGradientsOnRef( GradRef);

  for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit) {
        // collect some bnd information about the edges and verts of the tetra
        // and save it in IsOnDirBnd
        for (int i=0; i<4; ++i)
            IsOnDirBnd[i]= BndData.Vel.IsOnDirBnd( *sit->GetVertex(i) );
        for (int i=0; i<6; ++i)
            IsOnDirBnd[i+4]= BndData.Vel.IsOnDirBnd( *sit->GetEdge(i) );
        const IdxT prNumbTetra= sit->Unknowns(pidx);

        GetTrafoTr( T, det, *sit);
        absdet= std::fabs( det);

        // Setup B:   b(i,j) =  -\int psi_i * div( phi_j)
        for(int vel=0; vel<10; ++vel) {
            if (IsOnDirBnd[vel]) { // put coupling on rhs
                P2DiscCL::GetGradient( Grad_vel, GradRef[vel], T);
                tmp= vel<4 ? BndData.Vel.GetDirBndValue( *sit->GetVertex(vel), t)
                           : BndData.Vel.GetDirBndValue( *sit->GetEdge(vel-4), t);
                c->Data[ prNumbTetra]+= inner_prod( Grad_vel.quad( absdet), tmp);
            }
        }
    }
}


void SetupRhs2_P2P1( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL&, const StokesBndDataCL& BndData, VecDescCL* c, double t)
{
    ScopeTimerCL scope("SetupRhs2_P2P1");
    c->Clear( t);

    const Uint lvl = c->GetLevel();
    const Uint pidx= c->RowIdx->GetIdx();

    IdxT prNumb[4];
    bool IsOnDirBnd[10];

    Quad2CL<Point3DCL> Grad_vel, GradRef[10];

    SMatrixCL<3,3> T;

    double det, absdet;
    Point3DCL tmp;

    P2DiscCL::GetGradientsOnRef( GradRef);

    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit) {
        // collect some information about the edges and verts of the tetra
        // and save it in prNumb and IsOnDirBnd
        for (int i=0; i<4; ++i) {
            IsOnDirBnd[i]= BndData.Vel.IsOnDirBnd( *sit->GetVertex(i) );
            prNumb[i]= sit->GetVertex(i)->Unknowns(pidx);
        }
        for (int i=0; i<6; ++i)
            IsOnDirBnd[i+4]= BndData.Vel.IsOnDirBnd( *sit->GetEdge(i) );

        GetTrafoTr( T, det, *sit);
        absdet= std::fabs( det);

        // b(i,j) =  -\int psi_i * div( phi_j)
        for(int vel=0; vel<10; ++vel) {
            if (IsOnDirBnd[vel]) { // put coupling on rhs
                P2DiscCL::GetGradient( Grad_vel, GradRef[vel], T);
                tmp= vel<4 ? BndData.Vel.GetDirBndValue( *sit->GetVertex(vel), t)
                           : BndData.Vel.GetDirBndValue( *sit->GetEdge(vel-4), t);
                for(int pr=0; pr<4; ++pr)
                    c->Data[ prNumb[pr]]+= inner_prod( Grad_vel.quadP1( pr, absdet), tmp);
            }
        }
    }
}


void SetupRhs2_P2P1X( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL&, const StokesBndDataCL& BndData, VecDescCL* c, const LevelsetP2CL& lset, double t)
// P2 / P1X FEs (X=extended) for vel/pr
{
    ScopeTimerCL scope("SetupRhs2_P2P1X");

    c->Clear( t);
    const Uint lvl=  c->GetLevel();
    const Uint pidx= c->RowIdx->GetIdx();
    const ExtIdxDescCL& Xidx= c->RowIdx->GetXidx();
    IdxT prNumb[4];
    bool IsOnDirBnd[10];
    Quad2CL<Point3DCL> Grad_vel, GradRef[10];
    SMatrixCL<3,3> T;
    double det, absdet;
    Point3DCL tmp;
    InterfaceTetraCL cut;

    P2DiscCL::GetGradientsOnRef( GradRef);
    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit) {
        // collect some information about the edges and verts of the tetra
        // and save it in prNumb and IsOnDirBnd
        for (int i=0; i<4; ++i) {
            IsOnDirBnd[i]= BndData.Vel.IsOnDirBnd( *sit->GetVertex(i) );
            prNumb[i]= sit->GetVertex(i)->Unknowns(pidx);
        }
        for (int i=0; i<6; ++i)
            IsOnDirBnd[i+4]= BndData.Vel.IsOnDirBnd( *sit->GetEdge(i) );

        GetTrafoTr( T, det, *sit);
        absdet= std::fabs( det);
        cut.Init( *sit, *lset.PhiC, lset.GetBndData());
        const bool nocut= !cut.Intersects();

        // Setup B:   b(i,j) =  -\int psi_i * div( phi_j)
        for(int vel=0; vel<10; ++vel) {
            if (IsOnDirBnd[vel]) { // put coupling on rhs
                P2DiscCL::GetGradient( Grad_vel, GradRef[vel], T);
                tmp= vel<4 ? BndData.Vel.GetDirBndValue( *sit->GetVertex(vel), t)
                           : BndData.Vel.GetDirBndValue( *sit->GetEdge(vel-4), t);
                for(int pr=0; pr<4; ++pr) {
                    const Point3DCL int_grad_vel_pr= Grad_vel.quadP1( pr, absdet);
                    c->Data[ prNumb[pr]]+= inner_prod( int_grad_vel_pr, tmp);

                    if (nocut) continue;
                    // compute the integrals
                    // \int_{T_2} grad v_vel p_pr dx  -  C \int_{T}grad v_vel p_pr dx,
                    // where C = H(Phi(x_pr)) \in {0,1} and T_2 = T \cap \Omega_2
                    LocalP2CL<Point3DCL> gradv_p;
                    gradv_p[pr]= Grad_vel[pr];
                    for (int vert=0; vert<4; ++vert)
                        if (vert!=pr)
                            gradv_p[EdgeByVert(pr,vert)+4]= 0.25*(Grad_vel[pr]+Grad_vel[vert]);

                    Point3DCL integral= cut.GetSign(pr)==1 ? -int_grad_vel_pr : Point3DCL();

                    for (int ch=0; ch<8; ++ch)
                    {
                        cut.ComputeCutForChild(ch);
                        integral+= cut.quad( gradv_p, absdet, true); // integrate on positive part
                    }

                    c->Data[ Xidx[prNumb[pr]]]+= inner_prod( integral, tmp);
                }
            }
        }
    }
}


void SetupRhs2_P2P1D( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL&, const StokesBndDataCL& BndData, VecDescCL* c, double t)
{
    ScopeTimerCL scope("SetupRhs2_P2P1D");

    c->Clear( t);

    const Uint lvl         = c->GetLevel();
    const Uint pidx        = c->RowIdx->GetIdx();

    IdxT prNumb[4];
    bool IsOnDirBnd[10];

    Quad2CL<Point3DCL> Grad_vel, GradRef[10];

    SMatrixCL<3,3> T;

    double det, absdet;
    Point3DCL tmp;

    P2DiscCL::GetGradientsOnRef( GradRef);

    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit) {
        // collect some information about the edges and verts of the tetra
        // and save it in prNumb and IsOnDirBnd
        for (int i=0; i<4; ++i) {
            IsOnDirBnd[i]= BndData.Vel.IsOnDirBnd( *sit->GetVertex(i) );
            prNumb[i]= sit->GetVertex(i)->Unknowns(pidx);
        }
        for (int i=0; i<6; ++i)
            IsOnDirBnd[i+4]= BndData.Vel.IsOnDirBnd( *sit->GetEdge(i) );

        GetTrafoTr( T, det, *sit);
        absdet= std::fabs( det);

        // b(i,j) =  -\int psi_i * div( phi_j)
        for(int vel=0; vel<10; ++vel) {
            if (IsOnDirBnd[vel]) { // put coupling on rhs
                P2DiscCL::GetGradient( Grad_vel, GradRef[vel], T);
                tmp= vel<4 ? BndData.Vel.GetDirBndValue( *sit->GetVertex(vel), t)
                           : BndData.Vel.GetDirBndValue( *sit->GetEdge(vel-4), t);
                for(int pr=0; pr<4; ++pr)
                    c->Data[ prNumb[pr]]+= inner_prod( Grad_vel.quadP1D( pr, absdet), tmp);
            }
        }
    }
}


// -----------------------------------------------------------------------------
//                        Routines for SetupPrMass
// -----------------------------------------------------------------------------


void SetupPrMass_P0(const MultiGridCL& MG, const TwoPhaseFlowCoeffCL& Coeff, MatrixCL& matM, IdxDescCL& RowIdx, const LevelsetP2CL& lset)
{
    ScopeTimerCL scope("SetupPrMass_P0");

    const IdxT num_unks_pr=  RowIdx.NumUnknowns();
    MatrixBuilderCL M_pr(&matM, num_unks_pr,  num_unks_pr);

    const Uint lvl= RowIdx.TriangLevel();

    SmoothedJumpCL nu_invers( 1./Coeff.mu(0), 1./Coeff.mu(1), Coeff.mu);
    Quad2CL<double> nu_inv;
    LevelsetP2CL::const_DiscSolCL ls= lset.GetSolution();
    const Uint ls_lvl = ls.GetLevel();
    LocalP2CL<> locallset;
    const Uint pidx= RowIdx.GetIdx();

    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin(lvl),
         send= MG.GetTriangTetraEnd(lvl); sit != send; ++sit) {
        const double absdet= sit->GetVolume()*6.;
        if (ls_lvl != lvl) {
            locallset.assign( *sit, ls);
            nu_inv.assign( locallset);
        }
        else
            nu_inv.assign( *sit, ls);
        nu_inv.apply( nu_invers);

        const IdxT prNumbTetra= sit->Unknowns( pidx);
        M_pr( prNumbTetra, prNumbTetra)= nu_inv.quad( absdet);

    }
    M_pr.Build();
}


/// \brief Accumulator to set up the pressure matrix for P1/P1-XFEM.
class PrMassAccumulator_P1CL : public TetraAccumulatorCL
{
  protected:
    const MultiGridCL& MG;
    const PrincipalLatticeCL& lat;
    const TwoPhaseFlowCoeffCL& Coeff;
    MatrixCL& matM;
    IdxDescCL& RowIdx;
    const LevelsetP2CL& lset;

    std::valarray<double>     ls_loc_;
    TetraPartitionCL          partition_;

    const IdxT num_unks_pr;
    MatrixBuilderCL* M_pr;
    const Uint lvl;
    IdxT prNumb[4];
    double coup[4][4], coupT2[4][4];
    const double nu_inv_p, nu_inv_n;
    double integralp, integraln;
    InterfaceTetraCL cut;
    LocalP2CL<> pipj[4][4];
    LocalP2CL<> loc_phi;

    bool useXFEM;

    void local_setup ();
    void update_global_system ();


  public:
    PrMassAccumulator_P1CL (const MultiGridCL& MG_, const TwoPhaseFlowCoeffCL& Coeff_, MatrixCL& matM_, IdxDescCL& RowIdx_, const LevelsetP2CL& lset_, bool XFEM=true);

    ///\brief Initializes matrix-builders and load-vectors
    void begin_accumulation ();
    ///\brief Builds the matrices
    void finalize_accumulation();

    virtual void visit (const TetraCL& sit);

    virtual TetraAccumulatorCL* clone (int /*tid*/){ return new PrMassAccumulator_P1CL ( *this); };
};

PrMassAccumulator_P1CL::PrMassAccumulator_P1CL (const MultiGridCL& MG_, const TwoPhaseFlowCoeffCL& Coeff_, MatrixCL& matM_, IdxDescCL& RowIdx_, const LevelsetP2CL& lset_, bool XFEM)
    : MG(MG_), lat( PrincipalLatticeCL::instance( 2)), Coeff(Coeff_), matM(matM_), RowIdx(RowIdx_),
      lset(lset_), ls_loc_( lat.vertex_size()), num_unks_pr(RowIdx_.NumUnknowns()),
      lvl(RowIdx_.TriangLevel()), nu_inv_p(1./Coeff_.mu( 1.0)), nu_inv_n(1./Coeff_.mu( -1.0)), useXFEM( XFEM)
{
    for(int i= 0; i < 4; ++i) {
        for(int j= 0; j < i; ++j) {
            pipj[j][i][EdgeByVert( i, j) + 4]= 0.25;
            pipj[i][j][EdgeByVert( j, i) + 4]= 0.25;
        }
        pipj[i][i][i]= 1.;
        for (int vert= 0; vert < 3; ++vert)
            pipj[i][i][EdgeByVert( i, VertOfFace( i, vert)) + 4]= 0.25;
    }
}

void PrMassAccumulator_P1CL::begin_accumulation ()
{
    M_pr = new MatrixBuilderCL(&matM, num_unks_pr,  num_unks_pr);
}

void PrMassAccumulator_P1CL::finalize_accumulation()
{
    M_pr->Build();
    delete M_pr;
}

void PrMassAccumulator_P1CL::visit (const TetraCL& sit)
{
    const ExtIdxDescCL& Xidx= RowIdx.GetXidx();
    const double absdet= sit.GetVolume()*6.;
    loc_phi.assign( sit, *lset.PhiC, lset.GetBndData());
    cut.Init( sit, loc_phi);
    const bool nocut= !cut.Intersects();
    GetLocalNumbP1NoBnd( prNumb, sit, RowIdx);
    GridFunctionCL<> pp;
    QuadDomainCL q2dom_;
    bool sign[4];

    evaluate_on_vertexes( lset.GetSolution(), sit, lat, Addr( ls_loc_));
    partition_.make_partition<SortedVertexPolicyCL, MergeCutPolicyCL>( lat, ls_loc_);
    make_CompositeQuad2Domain( q2dom_, partition_);

    if (nocut) { // nu is constant in tetra
        const double nu_inv= cut.GetSign( 0) == 1 ? nu_inv_p : nu_inv_n;
        // write values into matrix
        for(int i=0; i<4; ++i)
            for(int j=0; j<4; ++j)
                (*M_pr)( prNumb[i], prNumb[j])+= nu_inv*P1DiscCL::GetMass( i, j)*absdet;
    }
    else { // nu is discontinuous in tetra
        for(int i=0; i<4; ++i) {
            sign[i]= cut.GetSign(i) == 1;
            for(int j=0; j<=i; ++j) {
                // compute the integrals
                // \int_{T_i} p_i p_j dx,    where T_i = T \cap \Omega_i, i=1,2
                integralp= integraln= 0.;
                resize_and_evaluate_on_vertexes( pipj[i][j], q2dom_, pp);
                integralp = quad( pp , absdet , q2dom_ , PosTetraC);
                integraln = quad( pp , absdet , q2dom_ , NegTetraC);

                coup[j][i]= integralp*nu_inv_p + integraln*nu_inv_n;
                coup[i][j]= coup[j][i];
                if (useXFEM) {
                    coupT2[j][i]= integralp*nu_inv_p;
                    coupT2[i][j]= coupT2[j][i];
                }
            }
        }

        // write values into matrix
        for(int i=0; i<4; ++i) {
            const IdxT xidx_i= (useXFEM ? Xidx[prNumb[i]] : NoIdx);
            for(int j= 0; j < 4; ++j) {
                (*M_pr)( prNumb[i], prNumb[j])+= coup[i][j];
                if (!useXFEM) continue;
                // tetra intersects Gamma => Xidx defined for all DoFs
                const IdxT xidx_j= Xidx[prNumb[j]];
                if (xidx_j!=NoIdx)
                    (*M_pr)( prNumb[i], xidx_j)+= coupT2[i][j] - sign[j] * coup[i][j];
                if (xidx_i!=NoIdx)
                    (*M_pr)( xidx_i, prNumb[j])+= coupT2[i][j] - sign[i] * coup[i][j];
                if (xidx_i!=NoIdx && xidx_j!=NoIdx && sign[i]==sign[j])
                    (*M_pr)( xidx_i, xidx_j)+= sign[i] ? coup[i][j] - coupT2[i][j] : coupT2[i][j];
            }
        }
    }
}


void SetupPrMass_P1(const MultiGridCL& MG, const TwoPhaseFlowCoeffCL& Coeff, MatrixCL& matM, IdxDescCL& RowIdx, const LevelsetP2CL& lset)
{
    ScopeTimerCL scope("SetupPrMass_P1");
    PrMassAccumulator_P1CL p1_accu( MG, Coeff, matM, RowIdx, lset, false);
    TetraAccumulatorTupleCL accus;
    MaybeAddProgressBar(MG, "PrMass(P1) Setup", accus, RowIdx.TriangLevel());
    accus.push_back( &p1_accu);
    accus( MG.GetTriangTetraBegin( RowIdx.TriangLevel()), MG.GetTriangTetraEnd( RowIdx.TriangLevel()));
}


void SetupPrMass_P1X(const MultiGridCL& MG, const TwoPhaseFlowCoeffCL& Coeff, MatrixCL& matM, IdxDescCL& RowIdx, const LevelsetP2CL& lset)
{
    ScopeTimerCL scope("SetupPrMass_P1X");
    PrMassAccumulator_P1CL p1_accu( MG, Coeff, matM, RowIdx, lset, true);
    TetraAccumulatorTupleCL accus;
    MaybeAddProgressBar(MG, "PrMass(P1X) Setup", accus, RowIdx.TriangLevel());
    accus.push_back( &p1_accu);
    accus( MG.GetTriangTetraBegin( RowIdx.TriangLevel()), MG.GetTriangTetraEnd( RowIdx.TriangLevel()));
}


void SetupPrMass_P1D(const MultiGridCL& MG, const TwoPhaseFlowCoeffCL& Coeff, MatrixCL& matM, IdxDescCL& RowIdx, const LevelsetP2CL& lset)
{
    ScopeTimerCL scope("SetupPrMass_P1D");

    const IdxT num_unks_pr=  RowIdx.NumUnknowns();
    MatrixBuilderCL M_pr(&matM, num_unks_pr,  num_unks_pr);

    const Uint lvl= RowIdx.TriangLevel();
    IdxT prNumb[4];

    SmoothedJumpCL nu_invers( 1./Coeff.mu(0), 1./Coeff.mu(1), Coeff.mu);
    Quad2CL<double> nu_inv;
    LevelsetP2CL::const_DiscSolCL ls= lset.GetSolution();
    const Uint ls_lvl = ls.GetLevel();
    LocalP2CL<> locallset;

    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin(lvl),
        send=MG.GetTriangTetraEnd(lvl); sit != send; ++sit) {
        const double absdet= sit->GetVolume()*6.;
        if (ls_lvl != lvl) {
            locallset.assign( *sit, ls);
            nu_inv.assign( locallset);
        }
        else
            nu_inv.assign( *sit, ls);
        nu_inv.apply( nu_invers);

        GetLocalNumbP1DNoBnd( prNumb, *sit, RowIdx);

        for(int i=0; i<4; ++i)    // assemble row prNumb[i]
            for(int j=0; j<4; ++j)
                M_pr( prNumb[i], prNumb[j])+= nu_inv.quadP1D( i, j, absdet);
    }
    M_pr.Build();
}

// -----------------------------------------------------------------------------
//                        Routines for SetupPrGhostStab_P1X
// -----------------------------------------------------------------------------

// These functions set up the stabilisation matrix C = -eps_p*J as described in the
// Master's thesis "On the Application of a Stabilised XFEM Technique..."
// by me (Matthias Kirchhart).

// In order to avoid polluting the namespace, put the helper functions in an
// anonymous one.
namespace
{
Uint get_face_number_in_tetra( const FaceCL *const F, const TetraCL *const K );
void get_tetra_to_face_indeces( const FaceCL *const F,
                                Uint K1idx_to_Fidx[4], Uint K2idx_to_Fidx[4] );
void get_tetra_corner_signs( const TetraCL *const K, const LevelsetP2CL &lset, int signs[4] );
void treat_face( const FaceCL *const F, const int set_number,
                 const IdxDescCL& RowIdx, const LevelsetP2CL &lset,
                 const double h3, const double mu_inv, MatrixBuilderCL &J_pr );
void get_stab_tetras( const MultiGridCL &MG, const IdxDescCL& RowIdx, const LevelsetP2CL& lset,
                      std::vector<const TetraCL*>& stab_tetras );
void get_stab_face_sets( const std::vector<const TetraCL*>& stab_tetras,
                         const LevelsetP2CL& lset,
                         std::set<const FaceCL*>& F_GammaOne,
                         std::set<const FaceCL*>& F_GammaTwo );
Ubyte is_in_F_Gamma_i( const TetraCL *const K, const Uint face_no,
                       const LevelsetP2CL &lset );

}

void SetupPrGhostStab_P1X( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL& Coeff,
                           MatrixCL& matC, IdxDescCL& RowIdx, const LevelsetP2CL& lset,
                           double eps_p )
{
    ScopeTimerCL scope( "SetupPrGhostStab_P1X" );

    const IdxT num_unks_pr = RowIdx.NumUnknowns();
    MatrixBuilderCL J_pr( &matC, num_unks_pr, num_unks_pr );

    std::vector<const TetraCL*> stab_tetras(0);
    get_stab_tetras( MG, RowIdx, lset, stab_tetras );

    std::set<const FaceCL*> F_GammaOne, F_GammaTwo;
    get_stab_face_sets( stab_tetras, lset, F_GammaOne, F_GammaTwo );

    // Use the volume as a measure of "h^3"
    // We use the minimum value instead of the maximum. Near the interface the
    // cells are typically smallest and it is the h in the vicinity of these
    // cells that is important for the stabilisation.
    double h3 = std::numeric_limits<double>::max();
    const Uint level = RowIdx.TriangLevel();
    typedef MultiGridCL::const_TriangTetraIteratorCL tetra_MG_iter;
    for ( tetra_MG_iter i = MG.GetTriangTetraBegin(level);
          i != MG.GetTriangTetraEnd(level); ++i )
    {
        h3 = std::min( h3, i->GetVolume() );
    }

    const double mu_inv1 = 1.0/Coeff.mu(0);
    const double mu_inv2 = 1.0/Coeff.mu(1);

    // Perform the actual stabilisation
    typedef std::set<const FaceCL*>::const_iterator face_iter;
    for ( face_iter i = F_GammaOne.begin();
          i != F_GammaOne.end(); ++i )
    {
        treat_face( *i, 1, RowIdx, lset, h3, mu_inv1, J_pr );
    }

    for ( face_iter i = F_GammaTwo.begin();
          i != F_GammaTwo.end(); ++i )
    {
        treat_face( *i, 2, RowIdx, lset, h3, mu_inv2, J_pr );
    }

    J_pr.Build();

    // We have assembled J, now multiply it by -eps_p to obtain C.
    matC *= -eps_p;
}

namespace
{

/*!
 * \brief Find all tetrahedra which are involved in the ghost stabilisation.
 * 
 * In the paper by Hansbo et al., the two sets of faces which are used in
 * the stabilisation, are defined via means of a set of tetrahedra:
 * those tetrahedra which are cut (\f$K_\Gamma\f$) and those which have more
 * than two neighbours in \f$K_\Gamma\f$, called \f$\tilde{K}_\Gamma\f$.
 * This method builds the union of these two sets and stores the result in
 * stab_tetras.
 */
void get_stab_tetras( const MultiGridCL &MG, const IdxDescCL& RowIdx,
                      const LevelsetP2CL& lset,
                      std::vector<const TetraCL*>& stab_tetras )
{
    // First, we need to find all cut elements.
    std::set<const TetraCL*> K_Gamma;

    const Uint level = RowIdx.TriangLevel();
    typedef MultiGridCL::const_TriangTetraIteratorCL tetra_MG_iter;
    for ( tetra_MG_iter i = MG.GetTriangTetraBegin(level);
          i != MG.GetTriangTetraEnd(level); ++i )
    {
        LocalP2CL<> loc_phi;
        loc_phi.assign( *i, lset.Phi, lset.GetBndData() );
        InterfaceTetraCL cut;
        cut.Init( *i, loc_phi );

        if ( cut.Intersects() )
        {
            K_Gamma.insert( &(*i) );    
        }
    }

    // Now find all cells that have more than one face in common with
    // cells in K_gamma. (Needed for LBB stability. The "blue" cells in
    // the master's thesis.
    std::set<const TetraCL*> K_Gamma_tilde;
    typedef std::set<const TetraCL*>::const_iterator set_tetra_iter;
    for ( set_tetra_iter i = K_Gamma.begin(); i != K_Gamma.end(); ++i )    
    {
        const TetraCL *const K = *i;
        for ( TetraCL::const_FacePIterator j = K->GetFacesBegin();
              j != K->GetFacesEnd(); ++j )
        {
            const FaceCL *const F = *j;
            const TetraCL *const K_neigh = F->GetNeighborTetra(K);
            if ( K_neigh == 0 ) continue;

            int neighbours_in_K_gamma = 0;
            for ( TetraCL::const_FacePIterator k = K_neigh->GetFacesBegin();
                  k != K_neigh->GetFacesEnd(); ++k )
            {
                const FaceCL *const FF = *k;
                const TetraCL *const candidate = FF->GetNeighborTetra(K_neigh);

                if ( K_Gamma.find( candidate ) != K_Gamma.end() )
                {
                    ++neighbours_in_K_gamma;
                }
            }
 
            if ( neighbours_in_K_gamma > 1 )
            {
                K_Gamma_tilde.insert( K_neigh );
            }
        }
    }
    // Now we have found all tetrahedra which we are interested in. Form a
    // new set without duplicates and clear the old ones.
    stab_tetras.resize( K_Gamma.size() + K_Gamma_tilde.size() );
    typedef std::vector<const TetraCL*>::iterator vec_tetra_iter;

    vec_tetra_iter tmp = std::set_union( K_Gamma.begin(), K_Gamma.end(),
                                         K_Gamma_tilde.begin(), K_Gamma_tilde.end(),
                                         stab_tetras.begin() );
    stab_tetras.resize( tmp - stab_tetras.begin() );
}

/*!
 * Given the set of stab_tetras from get_stab_tetras(), this function
 * builds the two sets of faces \f$F_{\Gamma,1}\f$ and \f$F_{\Gamma,2}\f$
 * which are involved in the stabilisation.
 */
void get_stab_face_sets( const std::vector<const TetraCL*>& stab_tetras,
                         const LevelsetP2CL& lset,
                         std::set<const FaceCL*>& F_GammaOne,
                         std::set<const FaceCL*>& F_GammaTwo )
{
    typedef std::vector<const TetraCL*>::const_iterator vec_tetra_iter;
    for ( vec_tetra_iter it = stab_tetras.begin();
          it != stab_tetras.end(); ++it )
    {
        const TetraCL *const K = *it;
        for ( Uint i = 0; i < 4; ++i )
        {
            Ubyte result = is_in_F_Gamma_i( K, i, lset );
            if ( result & 1 ) F_GammaOne.insert( K->GetFace(i) );
            if ( result & 2 ) F_GammaTwo.insert( K->GetFace(i) );
        }
    }
}

/*!
 * \brief Returns whether a face belongs to one of the stabilisation sets.
 *
 * The ghost stabilisation by Hansbo et al. is defined via two face sets, which
 * in turn are defined by means of a set of tetrahedra, see get_stab_tetras().
 * This function expects a tetrahedron from this set and the number of the face
 * within this tetrehedron. If the face lies at least partially in \f$\Omega_1\f$,
 * the least significant bit of the result is set. If it lies at least partially
 * in \f$\Omega_2\f$, the second lest significant bit of the result is set.
 */
Ubyte is_in_F_Gamma_i( const TetraCL *const K, const Uint face_no,
                       const LevelsetP2CL &lset )
{
    LocalP2CL<> loc_phi;
    loc_phi.assign( *K, lset.Phi, lset.GetBndData() );
    InterfaceTetraCL cut;
    cut.Init( *K, loc_phi );

    // There are ten degrees of freedom in the tetrahedron for the
    // level-set function. Here we obtain the local numbers for
    // those DOFs which lie on the face we are interested in.
    const int dof_numbers[] = { VertOfFace( face_no, 0 ),
                                VertOfFace( face_no, 1 ),
                                VertOfFace( face_no, 2 ),
                                EdgeOfFace( face_no, 0 ) + 4,
                                EdgeOfFace( face_no, 1 ) + 4,
                                EdgeOfFace( face_no, 2 ) + 4 };

    // Get the signs of the level-set functions at the DOFs of the
    // face.
    const int dof_signs[] = { cut.GetSign( dof_numbers[0] ),
                              cut.GetSign( dof_numbers[1] ),
                              cut.GetSign( dof_numbers[2] ),
                              cut.GetSign( dof_numbers[3] ),
                              cut.GetSign( dof_numbers[4] ),
                              cut.GetSign( dof_numbers[5] ) };

    Ubyte result = 0;
    // There are only two cases to consider:
    // 1. At least one level-set value is strictly negative (positive).
    //    In this case the face lies (at least) partially in \f$\Omega_1\f$
    //    (\f$\Omega_2\f$) and belongs to \f$F_{\Gamma,1}\f$ (\f$F_{\Gamma,2}\f$).
    // 2. The sign of the level-set function is exactly zero on all nodes.
    //    In this case the face is a subset of the interface and it belongs
    //    two both sets of faces for the stabilisation.

    // Case 1.
    if ( dof_signs[0] < 0 ) result |= 1;
    if ( dof_signs[1] < 0 ) result |= 1;
    if ( dof_signs[2] < 0 ) result |= 1;
    if ( dof_signs[3] < 0 ) result |= 1;
    if ( dof_signs[4] < 0 ) result |= 1;
    if ( dof_signs[5] < 0 ) result |= 1;

    if ( dof_signs[0] > 0 ) result |= 2;
    if ( dof_signs[1] > 0 ) result |= 2;
    if ( dof_signs[2] > 0 ) result |= 2;
    if ( dof_signs[3] > 0 ) result |= 2;
    if ( dof_signs[4] > 0 ) result |= 2;
    if ( dof_signs[5] > 0 ) result |= 2;
    
    // Case 2.
    if ( dof_signs[0] == dof_signs[1] && dof_signs[1] == dof_signs[2] &&
         dof_signs[2] == dof_signs[3] && dof_signs[3] == dof_signs[4] &&
         dof_signs[4] == dof_signs[5] && dof_signs[5] == 0 )
    {
        result |= 1;
        result |= 2;
    }

    return result;
}

/*!
 * \brief Add the contribution of a single face to the stabilisation matrix.
 *
 * Given a face and the number of the face set the face belongs to, this
 * function adds the contribution of that face to the stabilisation matrix.
 */
void treat_face( const FaceCL *const F, const int set_number,
                 const IdxDescCL& RowIdx, const LevelsetP2CL &lset,
                 const double h3, const double mu_inv, MatrixBuilderCL &J_pr )
{
    const TetraCL *const K1 = F->GetNeighbor(0);
    const TetraCL *const K2 = F->GetNeighbor(1);

    if ( K1 == 0 || K2 == 0 )
    {
        // F is part of the boundary and does not take part in the
        // stabilisation.
        return;
    }

    const Uint face_no1 = get_face_number_in_tetra( F, K1 );

    Point3DCL normal; double dir;
    const double area = 0.5*K1->GetNormal( face_no1, normal, dir );

    const double dir1 =  dir;
    const double dir2 = -dir;
    
    // There are 5 vertices involved: the three vertices of the face and
    // the respective opposite vertices in the tetrahedra. We introduce a
    // face-local numbering: 0 - 2 refer to the vertices on the face, 3
    // refers to the opposite vertex in K1, 4 to the opposite vertex in K2.

    Uint K1idx_to_Fidx[4], K2idx_to_Fidx[4];
    get_tetra_to_face_indeces( F, K1idx_to_Fidx, K2idx_to_Fidx );

    //////////////////////////////////////////////////////////////////
    // Compute the gradient jumps of the standard ansatz functions. //
    //////////////////////////////////////////////////////////////////
    double s_jumps[5] = { 0, 0, 0, 0, 0 };
    Point3DCL gradients[4]; double det;
    P1DiscCL::GetGradients( gradients, det, *K1 );
    for ( int i = 0; i < 4; ++i )
    {
        s_jumps[ K1idx_to_Fidx[i] ] += dir1*inner_prod( gradients[i], normal );
    }

    P1DiscCL::GetGradients( gradients, det, *K2 );
    for ( int i = 0; i < 4; ++i )
    {
        s_jumps[ K2idx_to_Fidx[i] ] += dir2*inner_prod( gradients[i], normal );
    }


    //////////////////////////////////////////////////////////////////
    // Compute the gradient jumps of the extended ansatz functions. //
    //////////////////////////////////////////////////////////////////
    double x_jumps[5] = { 0, 0, 0, 0, 0 };

    P1DiscCL::GetGradients( gradients, det, *K1 );

    /// Now retrieve the heaviside values for the extended ansatz functions.
    const int heaviside_face = ( set_number == 1 ) ? 0 : 1;

    int heaviside_nodes[4], signs[4];
    get_tetra_corner_signs( K1, lset, signs );
    for ( int i = 0; i < 4; ++i )
        heaviside_nodes[i] = ( signs[i] < 0 ) ? 0 : 1;

    // Change the signs of the gradients accordingly.
    for ( int i = 0; i < 4; ++i )
        gradients[i] *= (heaviside_face - heaviside_nodes[i]);
    
    // Add contribution to the jumps.
    for ( int i = 0; i < 4; ++i )
    {
        x_jumps[ K1idx_to_Fidx[i] ] += dir1*inner_prod( gradients[i], normal );
    }


    // The same, just for K2.
    P1DiscCL::GetGradients( gradients, det, *K2 );

    /// Now retrieve the \f$\Phi_j\f$ of the Ansatzfunctions.
    get_tetra_corner_signs( K2, lset, signs );
    for ( int i = 0; i < 4; ++i )
        heaviside_nodes[i] = ( signs[i] < 0 ) ? 0 : 1;

    // Change the signs of the gradients accordingly.
    for ( int i = 0; i < 4; ++i )
        gradients[i] *= (heaviside_face - heaviside_nodes[i]);

    
    // Add contribution to the jumps.
    for ( int i = 0; i < 4; ++i )
    {
        x_jumps[ K2idx_to_Fidx[i] ] += dir2*inner_prod( gradients[i], normal );
    }

    ////////////////////////////////////////////////////////////////////
    // Perform "integration" over the faces and add to global matrix. //
    ////////////////////////////////////////////////////////////////////
    const ExtIdxDescCL& Xidx = RowIdx.GetXidx();
    const Uint idx = RowIdx.GetIdx(); 
    IdxT indeces[10] = { NoIdx, NoIdx, NoIdx, NoIdx, NoIdx,
                         NoIdx, NoIdx, NoIdx, NoIdx, NoIdx };
    for ( int i = 0; i < 4; ++i )
    {
        IdxT std_idx = K1->GetVertex(i)->Unknowns(idx);
        indeces[     K1idx_to_Fidx[i] ] = std_idx;
        indeces[ 5 + K1idx_to_Fidx[i] ] = Xidx[ std_idx ];
    }
    for ( int i = 0; i < 4; ++i )
    {
        IdxT std_idx = K2->GetVertex(i)->Unknowns(idx);
        indeces[     K2idx_to_Fidx[i] ] = std_idx;
        indeces[ 5 + K2idx_to_Fidx[i] ] = Xidx[ std_idx ];
    }

    const double jumps[10] = { s_jumps[0], s_jumps[1], s_jumps[2], s_jumps[3], s_jumps[4],
                               x_jumps[0], x_jumps[1], x_jumps[2], x_jumps[3], x_jumps[4] };

    for ( int i = 0; i < 10; ++i )
    {
        if ( indeces[i] == NoIdx ) continue;
        if ( jumps[i] == 0 ) continue;

        for ( int j = 0; j < 10; ++j )
        {
            if ( indeces[j] == NoIdx ) continue;
            if ( jumps[j] == 0 ) continue;

            J_pr( indeces[i], indeces[j] ) += h3*mu_inv*area*jumps[i]*jumps[j];
        }
    }
}

void get_tetra_corner_signs( const TetraCL *const K, const LevelsetP2CL &lset, int signs[4] )
{
    LocalP2CL<> loc_phi;
    loc_phi.assign( *K, lset.Phi, lset.GetBndData() );
    InterfaceTetraCL cut;
    cut.Init( *K, loc_phi );

    signs[0] = cut.GetSign(0);  
    signs[1] = cut.GetSign(1);  
    signs[2] = cut.GetSign(2);  
    signs[3] = cut.GetSign(3);  
}

void get_tetra_to_face_indeces( const FaceCL *const F,
                                Uint K1idx_to_Fidx[4], Uint K2idx_to_Fidx[4] )
{
    const TetraCL *const K1 = F->GetNeighbor(0);
    const TetraCL *const K2 = F->GetNeighbor(1);

    for ( int i = 0; i < 4; ++i )
    {
        const VertexCL *const v = K1->GetVertex(i);
        if ( v == F->GetVertex(0) )
        {
            K1idx_to_Fidx[i] = 0;
        }
        else if ( v == F->GetVertex(1) )
        {
            K1idx_to_Fidx[i] = 1;
        }
        else if ( v == F->GetVertex(2) )
        {
            K1idx_to_Fidx[i] = 2;
        }
        else
        {
            K1idx_to_Fidx[i] = 3;
        }
    }

    for ( int i = 0; i < 4; ++i )
    {
        const VertexCL *const v = K2->GetVertex(i);
        if ( v == F->GetVertex(0) )
        {
            K2idx_to_Fidx[i] = 0;
        }
        else if ( v == F->GetVertex(1) )
        {
            K2idx_to_Fidx[i] = 1;
        }
        else if ( v == F->GetVertex(2) )
        {
            K2idx_to_Fidx[i] = 2;
        }
        else
        {
            K2idx_to_Fidx[i] = 4;
        }
    }
}

Uint get_face_number_in_tetra( const FaceCL *const F, const TetraCL *const K )
{
    if ( F == K->GetFace(0) ) return 0;
    if ( F == K->GetFace(1) ) return 1;
    if ( F == K->GetFace(2) ) return 2;
    if ( F == K->GetFace(3) ) return 3;
    else throw DROPSErrCL("Could not find the given face in the tetrahedron.");
}

} // end of anonymous namespace.

// -----------------------------------------------------------------------------
//                        Routines for SetupPrStiff
// -----------------------------------------------------------------------------


void SetupPrStiff_P1( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL& Coeff, MatrixCL& A_pr, IdxDescCL& RowIdx, IdxDescCL& ColIdx, const LevelsetP2CL& lset)
{
    ScopeTimerCL scope("SetupPrStiff_P1");

    MatrixBuilderCL A( &A_pr, RowIdx.NumUnknowns(), ColIdx.NumUnknowns());
    const Uint lvl= RowIdx.TriangLevel();
    const Uint idx= RowIdx.GetIdx();
    SMatrixCL<3,4> G;
    double coup[4][4];
    double det, absdet, IntRhoInv;
    IdxT UnknownIdx[4];

    const double rho_inv_p= 1./Coeff.rho(1.),
                 rho_inv_n= 1./Coeff.rho(-1.);
    LevelsetP2CL::const_DiscSolCL ls= lset.GetSolution();
    LocalP2CL<> loc_phi, ones( 1.);
    InterfaceTetraCL cut;

    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit)
    {
        P1DiscCL::GetGradients( G,det,*sit);
        absdet= std::fabs( det);
        loc_phi.assign( *sit, ls);
        cut.Init( *sit, loc_phi);
        if (!cut.Intersects()) {
            IntRhoInv= absdet/6*(cut.GetSign( 0) == 1 ? rho_inv_p : rho_inv_n);
        } else {
            double Vol_p=0, Vol_n=0;
            for (int ch= 0; ch < 8; ++ch) {
                cut.ComputeCutForChild( ch);
                Vol_p+= cut.quad( ones, absdet, true);  // integrate on positive part
                Vol_n+= cut.quad( ones, absdet, false); // integrate on negative part
            }
            IntRhoInv= Vol_p*rho_inv_p + Vol_n*rho_inv_n;
        }
        for(int i=0; i<4; ++i)
        {
            for(int j=0; j<=i; ++j)
            {
                // dot-product of the gradients
                coup[i][j]= ( G( 0, i)*G( 0, j) + G( 1, i)*G( 1, j) + G( 2, i)*G( 2, j) )*IntRhoInv;
                coup[j][i]= coup[i][j];
            }
            UnknownIdx[i]= sit->GetVertex( i)->Unknowns( idx);
        }
        for(int i=0; i<4; ++i)    // assemble row i
            for(int j=0; j<4;++j)
                A(UnknownIdx[i], UnknownIdx[j])+= coup[j][i];
    }
    A.Build();
}

// helper function to compute a P2 element as a product of 2 P1 elements in a tetrahedron
void computeLocalP2_pipj( LocalP2CL<> (&pipj)[4][4] ){

    //loop over 4 vertices of tetrahedron
    for(int i= 0; i < 4; ++i) {
        // product is symmetric, loop until i suffices
        for(int j= 0; j < i; ++j) {
            // only overlap of 2 p1 shape functions
            // is on the edge between them (unknown of p2 elem)
            Uint p2unknown = EdgeByVert( i , j ) + 4; // first 4 unknowns are the vertices of the tet
            // 1/2 * 1/2 = 0.25 -- values at midpoint of edge
            pipj[i][j][p2unknown] = pipj[j][i][p2unknown] = 0.25;
        }

        // product of p_i * p_i
        // one at node i
        pipj[i][i][i]= 1.;
        // compute values at outgoing 3 outgoing edges
        for (int vert= 0; vert < 3; ++vert)
        {
            // opposite face is i; get vertices of that face
            // this avoids getting vertex i again
            Uint neighborVertex = VertOfFace( i, vert );
            // as above get edgenumber and add 4  for vertex unknowns
            Uint p2unknown = EdgeByVert( i, neighborVertex ) + 4;
            // value at midpoint of edge
            pipj[i][i][p2unknown]= 0.25;

        }
    }
}

// helper function to compute a P2 element as a product of a P1 function and its gradient times normal in a tetrahedron
// the gradient of a P1 function is constant within an element and so the normal is constant on the interface segments
// hence the P2 function is the product of a P1 function times the 1-function
void computeLocalP2_gradpipj( LocalP2CL<> (&gradpipj)[4] ){

    //loop over 4 vertices of tetrahedron    
        for( int j=0; j < 4; ++j ){
            // basis function j is 1 on node j, 0 on other nodes
            gradpipj[j][j] = 1.0;
            // all edges are non-zero
            for( int k=4; k < 10; ++k){
                gradpipj[j][k] = 0.5;
            }
        }    
}

/// \todo: As in SetupPrMass_P1X, replace the smoothed density-function with integration
///        over the inner and outer part.
void SetupPrStiff_P1X( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL& Coeff, MatrixCL& A_pr, IdxDescCL& RowIdx, IdxDescCL& ColIdx, const LevelsetP2CL& lset, double lambda=1.0)
{
    ScopeTimerCL scope("SetupPrStiff_P1X");
    bool reduced = false;

    const ExtIdxDescCL& Xidx= RowIdx.GetXidx();
    MatrixBuilderCL A( &A_pr, RowIdx.NumUnknowns(), ColIdx.NumUnknowns());
    const Uint lvl= RowIdx.TriangLevel();
    const Uint idx= RowIdx.GetIdx();
    SMatrixCL<3,4> G;
    double coup[4][4], coupT2[4][4];
    double det;
    double absdet;
    IdxT UnknownIdx[4];
    bool sign[4];
    InterfaceTetraCL cut;

    const double rho_inv_p= 1./Coeff.rho(1.),
                 rho_inv_n= 1./Coeff.rho(-1.);
    LevelsetP2CL::const_DiscSolCL ls= lset.GetSolution();
    LocalP2CL<> locallset,
            ones( 1.), // for volume integration
            pipj[4][4], // for jump at the interface
            gradpipj[4];

    // triangle representing the interface, this is where the integration takes place
    InterfaceTriangleCL triang;

    // compute characteristic element length  h for Nitsche jump term lambda/h * int_gamma <[p_i],[p_j]>
    // use cubic root of minimal volume
    /*
    double h3 = std::numeric_limits<double>::max();
    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit)
    {
        h3 = std::min( h3, sit->GetVolume() );
    }
    // 6 * vol of tetra = vol of cube    
    double h = cbrt( 6 * h3 );
    */

    // compute values on reference tet for pipj
    // jump values at the interface correspond to values of p1 basis function at interface (jump to zero)
    // multiplied by +1/-1 depending on the location of the basis function
    // sign not relevant for jump terms (see below)
    computeLocalP2_pipj( pipj );

    computeLocalP2_gradpipj( gradpipj );
    //lambda = lambda / rho_min;

    //double lambda = 1; // read from input file as solver parameter

    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit)
    {
        //element matrix for jump terms
        //redefined and set to zero for every tetrahedron
        double coupJump[4][4] = {};
        double coupAv[4][4] = {};
        double h = cbrt( 6 * sit->GetVolume() );

        locallset.assign( *sit, ls);
        cut.Init( *sit, locallset);
        const bool nocut= !cut.Intersects();

        P1DiscCL::GetGradients( G,det,*sit);
        absdet= std::fabs( det);
        double IntRhoInv, IntRhoInv_p;
        double kappa1, kappa2;
        //kappa1=kappa2=0.5;
        //double Vol_p, Vol_n;
        double kappa_p,kappa_n, alphaK;


        if (nocut) {
            IntRhoInv_p= cut.GetSign( 0) == 1 ? absdet/6*rho_inv_p : 0;
            IntRhoInv= absdet/6*(cut.GetSign( 0) == 1 ? rho_inv_p : rho_inv_n);
        } else {
            double Vol_p=0, Vol_n=0;
            for (int ch= 0; ch < 8; ++ch) {
                cut.ComputeCutForChild( ch);
                Vol_p+= cut.quad( ones, absdet, true);  // integrate on positive part
                Vol_n+= cut.quad( ones, absdet, false); // integrate on negative part
            }
            double alpha_p = Vol_p / ( std::pow(h,3) );
            double alpha_n = Vol_n / ( std::pow(h,3) );
            alphaK = alpha_p + alpha_n;
            kappa_p = rho_inv_n * alpha_p / ( rho_inv_p * alpha_n + rho_inv_n * alpha_p );
            kappa_n = rho_inv_p * alpha_n / ( rho_inv_p * alpha_n + rho_inv_n * alpha_p );
            IntRhoInv_p= Vol_p*rho_inv_p;
            IntRhoInv=   Vol_p*rho_inv_p + Vol_n*rho_inv_n;
            kappa1 = Vol_n / ( Vol_p + Vol_n );
            kappa2 = Vol_p / ( Vol_p + Vol_n );

            //initialize triangle for cut tetrahedron
            triang.Init( *sit, locallset);

        }

        // compute local matrices
        for(int i=0; i<4; ++i)
        {
            for(int j=0; j<=i; ++j)
            {
                // dot-product of the gradients
                coup[i][j]= ( G( 0, i)*G( 0, j) + G( 1, i)*G( 1, j) + G( 2, i)*G( 2, j) )*IntRhoInv;
                coup[j][i]= coup[i][j];
            }
            UnknownIdx[i]= sit->GetVertex( i)->Unknowns( idx);
            if (nocut) continue; // extended basis functions have only support on tetra intersecting Gamma!

            sign[i]= cut.GetSign(i)==1;
            // assemble only diagonal of xfem block
            if( reduced )
            {
                coupT2[i][i] = ( G( 0, i)*G( 0, i) + G( 1, i)*G( 1, i) + G( 2, i)*G( 2, i) )*IntRhoInv_p;
            }
            else
            {
                for(int j=0; j<=i; ++j) {
                    // compute the integrals
                    // \int_{T_2} grad_i grad_j dx,    where T_2 = T \cap \Omega_2
                    coupT2[j][i]= ( G( 0, i)*G( 0, j) + G( 1, i)*G( 1, j) + G( 2, i)*G( 2, j) )*IntRhoInv_p;
                    coupT2[i][j]= coupT2[j][i];
                }
            }
        }

        //compute jump terms; on cut elements only
        // loop over refinement; 8 children for tetrahedron
        if( !nocut )
        {
            // compute {1/rho} for nitsche param
            double invRhoAv = 1;//IntRhoInv / ( absdet / 6 );
            double gammaK = 0.0;
            for( Uint iCh = 0; iCh < 8; ++iCh )
            {
                //compute possible cuts for child iCh
                triang.ComputeForChild( iCh );
                //compute normal for child patch
                Point3DCL ngamma = triang.GetNormal();
                // scalar product of gradpi, ngamma
                double graddotn[4] = {};
                for( int i = 0; i < 4; ++i ){                    
                    graddotn[i] = G(0,i)*ngamma[0] + G(1,i)*ngamma[1] + G(2,i)*ngamma[2];
                }

                // compute part of the integral on cut triangle 0,1,2 (no cut, triang cut, quad cut)                
                for( Uint cutTria = 0; cutTria < (Uint) triang.GetNumTriangles(); ++cutTria )
                {
                    gammaK += triang.quad2D(ones,cutTria);
                    //loop over possible combinations
                    for( int i = 0; i < 4; ++i )
                    {
                        for( int j = 0; j<=i; ++j )
                        {
                            coupJump[i][j] += triang.quad2D( pipj[i][j] , cutTria ) * invRhoAv; // add parts triangle
                            coupJump[j][i] = coupJump[i][j]; // symmetrize

                            // compute average terms
                            //coupAv[i][j] += graddotn[i] * triang.quad2D( gradpipj[j], cutTria );
                            //coupAv[j][i] += graddotn[j] * triang.quad2D( gradpipj[i], cutTria );

                        }
                    }                    
                }
            }            
            //some hack from hansbo paper

            gammaK = gammaK / h / h;
            double D = 0.5, C = 1.5;
            invRhoAv = kappa_n*rho_inv_n + kappa_p*rho_inv_p;
            double lambdaK = invRhoAv * ( D + C * gammaK / alphaK );
            for( int i=0 ; i < 4 ; ++i )
                for( int j=0; j < 4 ; ++j )
                    coupJump[i][j] *= lambdaK;


        }


        // write values into matrix
        for(int i=0; i<4; ++i)
        {
            for(int j=0; j<4; ++j)
                A(UnknownIdx[i], UnknownIdx[j])+= coup[j][i];
            if (nocut) continue; // extended basis functions have only support on tetra intersecting Gamma!

            const IdxT xidx_i= Xidx[UnknownIdx[i]];
            if (reduced)
            {
                if( xidx_i != NoIdx )
                {
                    A( xidx_i, xidx_i ) += sign[i] ? coup[i][i] - coupT2[i][i] : coupT2[i][i];
                    A( xidx_i, xidx_i ) += lambda / h * coupJump[i][i];// /rho_max;
                }
            }
            else
            {
                for(int j=0; j<4; ++j) // write values for extended basis functions
                {
                    const IdxT xidx_j= Xidx[UnknownIdx[j]];
                    if (xidx_j!=NoIdx)
                        A( UnknownIdx[i], xidx_j)+= coupT2[i][j] - sign[j]*coup[i][j]
                                                 + coupAv[i][j] * ( kappa1 * rho_inv_n + kappa2 * rho_inv_p );
                    if (xidx_i!=NoIdx)
                        A( xidx_i, UnknownIdx[j])+= coupT2[i][j] - sign[i]*coup[i][j]
                                                 + coupAv[j][i] * ( kappa1 * rho_inv_n + kappa2 * rho_inv_p );
                    if (xidx_i!=NoIdx && xidx_j!=NoIdx)
                    {
                        if( sign[i] == sign[j] )
                        {
                            A( xidx_i, xidx_j)+= sign[i] ? coup[i][j] - coupT2[i][j] : coupT2[i][j];
                            //in omega_2
                            if( sign[i] == 1 )
                                A( xidx_i, xidx_j) -= 2*kappa1*rho_inv_n * ( coupAv[i][j] + coupAv[j][i] );
                            //in omega_1
                            else
                                A( xidx_i, xidx_j) += 2*kappa2*rho_inv_p * ( coupAv[i][j] + coupAv[j][i] );
                        }
                        else
                        {
                            //i in omega_2
                            if( sign[i] ==1 )
                                A( xidx_i, xidx_j) += kappa2*rho_inv_p * coupAv[j][i] - kappa1*rho_inv_n * coupAv[i][j];
                            //j in omega_1
                            else
                                A( xidx_i, xidx_j) += kappa2*rho_inv_p * coupAv[i][j] - kappa1*rho_inv_n * coupAv[j][i];
                        }

                        //jump part -- has only effect on extended basis functions
                        // no distinction between location of wrt the interface: [f] := f|_1 - f|_2
                        // either f|_1 or f|_2 is zero, ext basis fcts have pos sign in omega_2 and neg sign in omega_1
                        // jumps do always have the same sign (namely negative), hence a positive product [px_i][px_j]
                        A( xidx_i, xidx_j) += lambda / h  * coupJump[i][j];// /rho_max;
                    }
                }
            }
        }
    }
    A.Build();
}


void SetupPrStiff_P1D( const MultiGridCL& MG, const TwoPhaseFlowCoeffCL& Coeff, MatrixCL& A_pr, IdxDescCL& RowIdx, IdxDescCL& ColIdx, const LevelsetP2CL& lset)
{
    ScopeTimerCL scope("SetupPrStiff_P1D");

    MatrixBuilderCL A( &A_pr,RowIdx.NumUnknowns(), ColIdx.NumUnknowns());
    const Uint lvl= RowIdx.TriangLevel();
    const Uint idx= RowIdx.GetIdx();
    SMatrixCL<3,4> G;
    double coup[4][4];
    double det;
    double absdet;
    IdxT UnknownIdx[4];

    SmoothedJumpCL rho_invers( 1./Coeff.rho(0), 1./Coeff.rho(1), Coeff.rho);
    Quad2CL<double> rho_inv;
    LevelsetP2CL::const_DiscSolCL ls= lset.GetSolution();
    const Uint ls_lvl = ls.GetLevel();
    LocalP2CL<> locallset;

    for (MultiGridCL::const_TriangTetraIteratorCL sit= MG.GetTriangTetraBegin( lvl),
         send= MG.GetTriangTetraEnd( lvl); sit != send; ++sit)
    {
        if (ls_lvl != lvl) {
            locallset.assign( *sit, ls);
            rho_inv.assign( locallset);
        }
        else
            rho_inv.assign( *sit, ls);
        rho_inv.apply( rho_invers);

        P1DDiscCL::GetGradients( G,det,*sit);
        absdet= std::fabs( det);
        const double IntRhoInv= rho_inv.quad( absdet);
        for(int i=0; i<4; ++i)
        {
            for(int j=0; j<=i; ++j)
            {
                // dot-product of the gradients
                coup[i][j]= ( G( 0, i)*G( 0, j) + G( 1, i)*G( 1, j) + G( 2, i)*G( 2, j) )*IntRhoInv;
                coup[j][i]= coup[i][j];
            }
            UnknownIdx[i]= sit->GetFace( i)->Unknowns( idx);
        }
        for(int i=0; i<4; ++i)    // assemble row i
            for(int j=0; j<4;++j)
                A(UnknownIdx[i], UnknownIdx[j])+= coup[j][i];
    }
    A.Build();
}

// =============================================================================
//                        InstatStokes2PhaseP2P1CL
// =============================================================================

// Create numbering
// ----------------

void InstatStokes2PhaseP2P1CL::CreateNumberingVel( Uint level, MLIdxDescCL* idx, match_fun match, const LevelsetP2CL* lsetp)
{
    idx->CreateNumbering( level, MG_, BndData_.Vel, match, lsetp ? &(lsetp->Phi) : 0, lsetp ? &(lsetp->GetBndData()) : 0);
}


void InstatStokes2PhaseP2P1CL::CreateNumberingPr ( Uint level, MLIdxDescCL* idx, match_fun match, const LevelsetP2CL* lsetp)
{
    idx->CreateNumbering( level, MG_, BndData_.Pr, match, lsetp ? &(lsetp->Phi) : 0, lsetp ? &(lsetp->GetBndData()) : 0);
}


void InstatStokes2PhaseP2P1CL::SmoothVel( VelVecDescCL* v, int num, double tau)
{
    const VectorCL diag= A.Data.GetFinest().GetDiag();

    for (int i=0; i<num; ++i)
        v->Data-= tau*((A.Data.GetFinest()*v->Data)/diag);
}


void InstatStokes2PhaseP2P1CL::SetupPrMass( MLMatDescCL* matM, const LevelsetP2CL& lset) const
/// Needed for preconditioning of the Schur complement. Uses natural
/// boundary conditions for the pressure unknowns.
{
    MLMatrixCL::iterator itM = matM->Data.begin();
    MLIdxDescCL::iterator itIdx = matM->RowIdx->begin();
    for (size_t lvl=0; lvl < matM->Data.size(); ++lvl, ++itM, ++itIdx)
    {
        switch (GetPrFE())
        {
        case P0_FE:
            SetupPrMass_P0( MG_, Coeff_, *itM, *itIdx, lset); break;
        case P1_FE:
            SetupPrMass_P1( MG_, Coeff_, *itM, *itIdx, lset); break;
        case P1X_FE:
            SetupPrMass_P1X( MG_, Coeff_, *itM, *itIdx, lset); break;
        case P1D_FE:
            SetupPrMass_P1D( MG_, Coeff_, *itM, *itIdx, lset); break;
        default:
            throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupPrMass not implemented for this FE type");
        }
    }
}

void InstatStokes2PhaseP2P1CL::SetupC( MLMatDescCL* matC, const LevelsetP2CL& lset, double eps_p ) const
{
    MLMatrixCL::iterator itC = matC->Data.begin();
    MLIdxDescCL::iterator itIdx = matC->RowIdx->begin();
    for ( size_t lvl = 0; lvl < matC->Data.size(); ++lvl, ++itC, ++itIdx )
    {
        if ( GetPrFE() == P1X_FE )
        {
            SetupPrGhostStab_P1X( MG_, Coeff_, *itC, *itIdx, lset, eps_p );
        }
        else
        { 
            // Stabilisation is not defined for this type. Don't do anything.
            const IdxT num_unks_pr = itIdx->NumUnknowns();
            itC->clear();
            itC->resize( num_unks_pr, num_unks_pr, 0 );
        }
    }

    //delete old version of kernel (if there is any)
    //if( cKernel != NULL )
      //  delete cKernel;
    //compute new  kernel of c
    //cKernel = new CkernelCL( MG_, lset, pr_idx.GetFinest() );
    if ( GetPrFE() == P1X_FE )
    {
        computeGhostPenaltyKernel( MG_ , lset , pr_idx.GetFinest() );
    }

}

void InstatStokes2PhaseP2P1CL::computeGhostPenaltyKernel(MultiGridCL &mg, const LevelsetP2CL &lset, const IdxDescCL &prIdx) const
{
    Uint lvl = mg.GetNumLevel() - 1;
    //Uint size = mg_.GetTriangVertex().size();
    IdxT size = prIdx.NumUnknowns();
    ExtIdxDescCL xidxdesc = prIdx.GetXidx();

    //cKernelVecs = VectorBaseCL<VectorCL>( VectorCL(0.0, (size_t)size), 8 );
    cKernel = VectorBaseCL<VectorCL>( VectorCL(0.0, (size_t)size), 8 );

    for(MultiGridCL::TriangVertexIteratorCL vit   = mg.GetTriangVertexBegin(lvl),
        vend  = mg.GetTriangVertexEnd(lvl) ; vit!=vend ; ++vit)
    {
        IdxT idx = vit->Unknowns( prIdx.GetIdx() );
        IdxT xidx = xidxdesc[idx];

        double xcoord,ycoord,zcoord;

        Point3DCL coord = vit->GetCoord();
        xcoord  = coord[0];
        ycoord  = coord[1];
        zcoord  = coord[2];

        // const function
        cKernel[0][idx] = 1;
        // linear function in x direction
        cKernel[1][idx] = xcoord;
        // linear function in y direction
        cKernel[2][idx] = ycoord;
        // linear function in z direction
        cKernel[3][idx] = zcoord;

        if ( InterfacePatchCL::Sign( lset.GetSolution().val(*vit) ) != 1 )
        {
            // const func in omega 1
            cKernel[4][idx] = 1;
            // lin func in x in omega 1
            cKernel[5][idx] = xcoord;
            // lin func in y in omega 1
            cKernel[6][idx] = ycoord;
            // lin func in z in omega 1
            cKernel[7][idx] = zcoord;
        }
        if ( xidx != NoIdx )
        {
            // in omega 1: ext basis funs have opposite sign
            cKernel[4][xidx] = -1;
            cKernel[5][xidx] = -xcoord;
            cKernel[6][xidx] = -ycoord;
            cKernel[7][xidx] = -zcoord;
        }

    }

}

void InstatStokes2PhaseP2P1CL::SetupPrStiff( MLMatDescCL* A_pr, const LevelsetP2CL& lset, double lambda ) const
/// Needed for preconditioning of the Schur complement. Uses natural
/// boundary conditions for the pressure unknowns.
{
    MLMatrixCL::iterator itM = A_pr->Data.begin();
    MLIdxDescCL::iterator itRowIdx = A_pr->RowIdx->begin();
    MLIdxDescCL::iterator itColIdx = A_pr->ColIdx->begin();
    for (size_t lvl=0; lvl < A_pr->Data.size(); ++lvl, ++itM, ++itRowIdx, ++itColIdx)
    {
        switch (GetPrFE())
        {
        case P1_FE:
            SetupPrStiff_P1( MG_, Coeff_, *itM, *itRowIdx, *itColIdx, lset); break;
        case P1X_FE:
            SetupPrStiff_P1X( MG_, Coeff_, *itM, *itRowIdx, *itColIdx, lset, lambda); break;
        case P1D_FE:
            SetupPrStiff_P1D( MG_, Coeff_, *itM, *itRowIdx, *itColIdx, lset); break;
        default:
            throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupPrStiff not implemented for this FE type");
        }
    }
}


void InstatStokes2PhaseP2P1CL::InitVel(VelVecDescCL* vec, instat_vector_fun_ptr LsgVel, double t0) const
{
    VectorCL& lsgvel= vec->Data;
    vec->t = t0;
    const Uint lvl  = vec->GetLevel(),
               vidx = vec->RowIdx->GetIdx();

    for (MultiGridCL::const_TriangVertexIteratorCL sit= const_cast<const MultiGridCL&>( MG_).GetTriangVertexBegin( lvl),
         send= const_cast<const MultiGridCL&>( MG_).GetTriangVertexEnd( lvl);
         sit != send; ++sit) {
        if ( sit->Unknowns.Exist() && sit->Unknowns.Exist( vidx))
            DoFHelperCL<Point3DCL, VectorCL>::set( lsgvel, sit->Unknowns( vidx),
                LsgVel(sit->GetCoord(), t0));
    }
    for (MultiGridCL::const_TriangEdgeIteratorCL sit= const_cast<const MultiGridCL&>( MG_).GetTriangEdgeBegin( lvl),
         send= const_cast<const MultiGridCL&>( MG_).GetTriangEdgeEnd( lvl);
         sit != send; ++sit) {
        if ( sit->Unknowns.Exist() && sit->Unknowns.Exist( vidx))
            DoFHelperCL<Point3DCL, VectorCL>::set( lsgvel, sit->Unknowns( vidx),
                LsgVel( (sit->GetVertex(0)->GetCoord() + sit->GetVertex(1)->GetCoord())/2., t0));
    }
}


/// \brief Raw data for "system 1", both for one phase and two phases.
///
/// scalar-valued mass-matrix, scalar-valued mu-Laplacian, genuinely tensor-valued part of the deformation tensor and the integrals of \f$\rho\phi_i\f$ for the gravitation as load-vector
/// \todo: Precise description
struct LocalSystem1DataCL
{
    double         M [10][10];
    double         A [10][10];
    SMatrixCL<3,3> Ak[10][10];

    double rho_phi[10];
};

/// \brief Update the local system 1 (nocut) with respect to special boundary conditions: slip Bnd and symmetric Bnd;
class SpecialBndHandler_System1OnePhaseP2CL
{
  private:
    const StokesBndDataCL& BndData_;
	double mu_;
	double beta_;      //Slip coefficient, beta_=0 for symmetric Bnd;
	const double alpha_;     //Coefficient for Nitche method
	LocalP1CL<Point3DCL> Grad[10], GradRef[10];
	Point3DCL normal;
	Uint unknownIdx[6];
  public:	
	SpecialBndHandler_System1OnePhaseP2CL(const StokesBndDataCL& BndData, const double alpha=0): BndData_(BndData), alpha_(alpha)
    { P2DiscCL::GetGradientsOnRef( GradRef); 
	}
	void setMu(double mu){mu_=mu;}
	void setBeta(double beta){beta_=beta;}
    void setup(const TetraCL& tet, const SMatrixCL<3,3>& T, LocalSystem1DataCL& loc);  //update local system 1
	void setupRhs(const TetraCL& tet, Point3DCL loc_b[10], double t);                            //for no homogenour slip boundary condition (the slip wall is moving)
};

void SpecialBndHandler_System1OnePhaseP2CL::setup(const TetraCL& tet, const SMatrixCL<3,3>& T, LocalSystem1DataCL& loc)
{ 

    P2DiscCL::GetGradients( Grad, GradRef, T);
	for (Uint k =0; k< 4; ++k) //Go throught all faces of a tet
	{
		SMatrixCL<3, 3> dm[10][10];
		LocalP2CL<double> phi[10]; 
		Quad5_2DCL<double> mass2Dj;
		Quad5_2DCL<double> mass2Di;
		Quad5_2DCL<double> Grad2Dj;   // \nabla phi_j* n
		Quad5_2DCL<double> Grad2Di;   // \nabla phi_i* n
		BaryCoordCL bary[3];
		bool symmBC=false;
		if( BndData_.Vel.GetBC(*tet.GetFace(k))==Slip0BC || BndData_.Vel.GetBC(*tet.GetFace(k))==SlipBC|| BndData_.Vel.GetBC(*tet.GetFace(k))==SymmBC){
			if(BndData_.Vel.GetBC(*tet.GetFace(k))==SymmBC)
				symmBC=true;
			const FaceCL& face = *tet.GetFace(k);
            double absdet = FuncDet2D(	face.GetVertex(1)->GetCoord()-face.GetVertex(0)->GetCoord(),
                                           	face.GetVertex(2)->GetCoord()-face.GetVertex(0)->GetCoord()); 
			double h= std::sqrt(absdet);
			tet.GetOuterNormal(k, normal);
			for (Uint i= 0; i<3; ++i)    
			{
				bary[i][VertOfFace(k, i)]=1;
			}
			for(Uint i=0; i<10; ++i)
				phi[i][i] = 1;
				
		    double temp = symmBC? 0.: beta_;
			for(Uint i=0; i<10; ++i){
				LocalP1CL<double> Gradin(dot( normal, Grad[i]));
				mass2Di.assign(phi[i], bary);	
				Grad2Di.assign(Gradin, bary);
				for(Uint j=0; j<=i; ++j){
					LocalP1CL<double> Gradjn(dot( normal, Grad[j]));
					mass2Dj.assign(phi[j], bary);  //
					Grad2Dj.assign(Gradjn, bary);
					Quad5_2DCL<double> mass2D(mass2Dj * mass2Di); //
					Quad5_2DCL<double> Grad2D(Grad2Di * mass2Dj + Grad2Dj * mass2Di); //
					// three additional terms
					dm[j][i](0, 0)=dm[j][i](1, 1) = dm[j][i](2, 2) = temp * mass2D.quad(absdet);
					dm[j][i]     += (alpha_/h * mu_ - temp) * mass2D.quad(absdet) * SMatrixCL<3,3> (outer_product(normal, normal));
					// if(BndData_.Vel.GetBC(*tet.GetFace(k))!= SymmBC) not necessary
					dm[j][i]     -=  2. * mu_ * Grad2D.quad(absdet) * SMatrixCL<3,3> (outer_product(normal, normal));  
					loc.Ak[j][i] += dm[j][i];
					if (i != j){
						assign_transpose( dm[i][j], dm[j][i]);
						loc.Ak[i][j] += dm[i][j];
					}	
				}
			}
		}
	}
}

//for non homogeneous slip boundary condition (the slip wall is moving)
void SpecialBndHandler_System1OnePhaseP2CL::setupRhs(const TetraCL& tet, Point3DCL loc_b[10], double t)
{
	for (Uint k =0; k< 4; ++k){ //Go throught all faces of a tet
	
		LocalP2CL<double> phi[6]; 
		Quad5_2DCL<double> locP2[6];
		Quad5_2DCL<Point3DCL> WallVel;
		BaryCoordCL bary[3];
		if( BndData_.Vel.GetBC(*tet.GetFace(k))==SlipBC ){
			const FaceCL& face = *tet.GetFace(k);
            double absdet = FuncDet2D(	face.GetVertex(1)->GetCoord()-face.GetVertex(0)->GetCoord(),
                                           	face.GetVertex(2)->GetCoord()-face.GetVertex(0)->GetCoord()); 
			tet.GetOuterNormal(k, normal);
			for (Uint i= 0; i<3; ++i)    
			{
				unknownIdx[i]   = VertOfFace(k, i);      // i is index for Vertex
				unknownIdx[i+3] = EdgeOfFace(k, i) + 4;  // i is index for Edge
				bary[i][unknownIdx[i]]=1;
			}
			typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
			bnd_val_fun bf= BndData_.Vel.GetBndSeg(face.GetBndIdx()).GetBndFun();
			WallVel.assign(tet, bary, bf, t);
			for(Uint i=0; i<6; ++i)
				phi[i][unknownIdx[i]] = 1;
				
			for(Uint i=0; i<6; ++i){
					locP2[i].assign(phi[i], bary);
			}

			for(Uint i=0; i<6; ++i){//setup right hand side	
					Quad5_2DCL<Point3DCL> WallVelRhs( locP2[i]* WallVel);
					loc_b[unknownIdx[i]] += (beta_ * WallVelRhs.quad(absdet) - beta_ * SMatrixCL<3,3> (outer_product(normal, normal)) * WallVelRhs.quad(absdet));
			}		
		}
	}	
}

/// \brief Setup of the local "system 1" on a tetra in a single phase.
class LocalSystem1OnePhase_P2CL
{
  private:
    double mu_;
    double rho_;

    Quad2CL<Point3DCL> Grad[10], GradRef[10];
    const SVectorCL<Quad2DataCL::NumNodesC> Ones;

  public:
    LocalSystem1OnePhase_P2CL (double muarg= 0., double rhoarg= 0.)
        : mu_( muarg), rho_( rhoarg), Ones( 1.)
    { P2DiscCL::GetGradientsOnRef( GradRef); }

    void   mu  (double new_mu)        { mu_= new_mu; }
    double mu  ()               const { return mu_; }
    void   rho (double new_rho)       { rho_= new_rho; }
    double rho ()               const { return rho_; }

    void setup (const SMatrixCL<3,3>& T, double absdet, LocalSystem1DataCL& loc);
};

void LocalSystem1OnePhase_P2CL::setup (const SMatrixCL<3,3>& T, double absdet, LocalSystem1DataCL& loc)
{
    P2DiscCL::GetGradients( Grad, GradRef, T);
    for (Uint i= 0; i < 10; ++i) {
        loc.rho_phi[i]= rho()*quad( Ones, absdet, Quad2Data_Mul_P2_CL(), i);
        for (Uint j= 0; j <= i; ++j) {
            // M: As we are not at the phase-boundary this is exact.
            loc.M[j][i]= rho()*P2DiscCL::GetMass( j, i)*absdet;

            // kreuzterm = \int mu * (dphi_i / dx_l) * (dphi_j / dx_k) = \int mu *\nabla\phi_i \outerprod \nabla\phi_j
            loc.Ak[j][i]= mu()*quad( OuterProductExpressionCL( Grad[i], Grad[j]), absdet, make_Quad2Data());
            // dot-product of the gradients
            loc.A[j][i]= trace( loc.Ak[j][i]);
            if (i != j) { // The local matrices coupM, coupA, coupAk are symmetric.
                loc.M[i][j]= loc.M[j][i];
                loc.A[i][j]= loc.A[j][i];
                assign_transpose( loc.Ak[i][j], loc.Ak[j][i]);
            }
        }
    }
}

struct LocalBndIntegrals_P2CL
{
    double         mass2D[10][10];
    double         grad2D[10][10];
};

/// \brief Place to store P2 integrals on positive/negative part of tetrahedron
/// to be shared by LocalSystem1TwoPhase_P2CL and LocalSystem1TwoPhase_P2XCL
struct LocalIntegrals_P2CL
{
    double         phi[10];
    Point3DCL      rhs[10];
    double         mass[10][10];
    SMatrixCL<3,3> cAk[10][10];
};

/// \brief Update the local system 1 due to special (cut) boundary conditions: slip Bnd and symmetric Bnd;
class SpecialBndHandler_System1TwoPhaseP2CL
{
  private:
    const PrincipalLatticeCL& lat;
    const StokesBndDataCL& BndData_;

    const VecDescCL&    Phi_;
    const BndDataCL<>& lsetbnd_;
    InterfaceTriangleCL triangle;

    instat_vector_fun_ptr outnormal_;//outnormal of the domain boundary

    bool SpeBnd; //special boundary condition
    const double mu1_, mu2_;                                //dynamic viscosities
    const double beta1_, beta2_;                            //Slip length, beta1_=beta2_=0 for symmetric Bnd;
    const double betaL_; 
    const double alpha_;                                    //Coefficient for Nitche method
    LocalP1CL<Point3DCL> Grad[10], GradRef[10];
    LocalP2CL<> p2;

    std::valarray<double> ls_loc;
    BndTriangPartitionCL partition;
    QuadDomainCL q5dom;
    Point3DCL normal;
    GridFunctionCL<double>   basisP2[10];
    GridFunctionCL<double>   gradP1[10];
  public:	
	SpecialBndHandler_System1TwoPhaseP2CL(const StokesBndDataCL& BndData, const VecDescCL& Phi, const BndDataCL<>& lsetbnd, instat_vector_fun_ptr outnormal, double mu1, double mu2, const double beta1=0, const double beta2=0, const double betaL=0, const double alpha=0)
	: lat( PrincipalLatticeCL::instance( 2)), BndData_(BndData), Phi_(Phi), lsetbnd_(lsetbnd), outnormal_(outnormal), mu1_(mu1), mu2_(mu2), beta1_(beta1), beta2_(beta2), betaL_(betaL), alpha_(alpha), ls_loc( lat.vertex_size())
    { P2DiscCL::GetGradientsOnRef( GradRef); }

    double mu  (int sign) const { return sign > 0 ?  mu1_  : mu2_; }
    double beta (int sign) const { return sign > 0 ? beta1_ : beta2_; }
    void setup(const TetraCL& tet, const SMatrixCL<3,3>& T, const LocalP2CL<>& ls, LocalSystem1DataCL& loc);  //update local system 1
    void setupRhs(const TetraCL& tet, Point3DCL loc_b[10], double t);
    void CLdiss(const TetraCL& tet, LocalSystem1DataCL& loc); //Set up contact line dissipation term
};

void SpecialBndHandler_System1TwoPhaseP2CL::setup(const TetraCL& tet, const SMatrixCL<3,3>& T, const LocalP2CL<>& ls,  LocalSystem1DataCL& loc)
{ 

	LocalBndIntegrals_P2CL locInt[2];
    P2DiscCL::GetGradients( Grad, GradRef, T);
	

	for (Uint k =0; k< 4; ++k) //Go through all faces of a tet
	{
		SMatrixCL<3, 3> dm[10][10];
		LocalP2CL<double> phi[10]; 
	    LocalP1CL<double> Gradn[10];
		BaryCoordCL bary[3];
		bool symmBC=false;
		if( BndData_.Vel.GetBC(*tet.GetFace(k))==Slip0BC || BndData_.Vel.GetBC(*tet.GetFace(k))==SlipBC|| BndData_.Vel.GetBC(*tet.GetFace(k))==SymmBC){
			if(BndData_.Vel.GetBC(*tet.GetFace(k))==SymmBC)
				symmBC=true;
			const FaceCL& face = *tet.GetFace(k);
            double absdet = FuncDet2D(	face.GetVertex(1)->GetCoord()-face.GetVertex(0)->GetCoord(),
                                           	face.GetVertex(2)->GetCoord()-face.GetVertex(0)->GetCoord()); 
			double h= std::sqrt(absdet);
			tet.GetOuterNormal(k, normal);
			
			evaluate_on_vertexes( ls, lat, Addr( ls_loc));  //Get level set values
			partition.make_partition2D<SortedVertexPolicyCL, MergeCutPolicyCL>(lat, k, ls_loc);

			make_CompositeQuad5BndDomain2D(q5dom, partition, tet);

			for (Uint i= 0; i<3; ++i)    
			{
				bary[i][VertOfFace(k, i)]=1;
			}
			for(Uint i=0; i<10; ++i)
			{
				phi[i][i] = 1;
				Gradn[i] = dot( normal, Grad[i]); 
			}
			
			for(Uint i=0; i<10; ++i)
			{
				resize_and_evaluate_on_vertexes( phi[i], q5dom, basisP2[i]); // for M
				resize_and_evaluate_on_vertexes( Gradn[i], q5dom, gradP1[i]); // for A	
			}

				DROPS::GridFunctionCL<> integrand( 1., q5dom.vertex_size()); // Gridfunction with constant 1 everywhere
				double area_neg, area_pos;
				area_neg =quad( integrand, q5dom, DROPS::NegTetraC);
				area_pos =quad( integrand, q5dom, DROPS::PosTetraC);
              double h1 = std::sqrt(area_pos)>0.1*h ? std::sqrt(area_pos): 0.1*h;
              double h2 = std::sqrt(area_neg)>0.1*h ? std::sqrt(area_neg): 0.1*h;
		    double temp1 = symmBC? 0: beta1_;
			double temp2 = symmBC? 0: beta2_;
			for(Uint i=0; i<10; ++i){
				for(Uint j=0; j<=i; ++j){	
                    quad( basisP2[i] * basisP2[j], q5dom, locInt[0].mass2D[i][j], locInt[1].mass2D[i][j]);			
                    quad( gradP1[i] * basisP2[j] + gradP1[j] * basisP2[i], q5dom, locInt[0].grad2D[i][j], locInt[1].grad2D[i][j]);
					// three additional terms
					dm[j][i](0, 0)= dm[j][i](1, 1) = dm[j][i](2, 2) = temp1 * locInt[0].mass2D[i][j] + temp2* locInt[1].mass2D[i][j];
					dm[j][i]     += ( (alpha_/h1*mu1_ -temp1)* locInt[0].mass2D[i][j] + (alpha_/h2* mu2_ - temp2) * locInt[1].mass2D[i][j] )* SMatrixCL<3,3> (outer_product(normal, normal));
                  //dm[j][i]     += ( (alpha_/h1*mu1_)* locInt[0].mass2D[i][j] + (alpha_/h2* mu2_ ) * locInt[1].mass2D[i][j] )* SMatrixCL<3,3> (outer_product(normal, normal));
					//if(BndData_.Vel.GetBC(*tet.GetFace(k))!= SymmBC) not necessary
					dm[j][i]     -= ( 2. * mu1_ * locInt[0].grad2D[i][j]+ 2.* mu2_ * locInt[1].grad2D[i][j] )* SMatrixCL<3,3> (outer_product(normal, normal));  
					loc.Ak[j][i] += dm[j][i];
					if (i != j){
						assign_transpose( dm[i][j], dm[j][i]);
						loc.Ak[i][j] += dm[i][j];
					}	
				}
			}
		}
	}
}

//for non homogeneous slip boundary condition (the slip wall is moving)
void SpecialBndHandler_System1TwoPhaseP2CL::setupRhs(const TetraCL& tet, Point3DCL loc_b[10], double t)
{
    Uint unknownIdx[6];
	for (Uint k =0; k< 4; ++k){ //Go throught all faces of a tet
	
		LocalP2CL<double> phi[6]; 
		Quad5_2DCL<double> locP2[6];
		Quad5_2DCL<Point3DCL> WallVel;
		BaryCoordCL bary[3];
		if( BndData_.Vel.GetBC(*tet.GetFace(k))==SlipBC ){
			const FaceCL& face = *tet.GetFace(k);
            double absdet = FuncDet2D(	face.GetVertex(1)->GetCoord()-face.GetVertex(0)->GetCoord(),
                                           	face.GetVertex(2)->GetCoord()-face.GetVertex(0)->GetCoord()); 
			tet.GetOuterNormal(k, normal);
			for (Uint i= 0; i<3; ++i)    
			{
				unknownIdx[i]   = VertOfFace(k, i);      // i is index for Vertex
				unknownIdx[i+3] = EdgeOfFace(k, i) + 4;  // i is index for Edge
				bary[i][unknownIdx[i]]=1;
			}
			typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
			bnd_val_fun bf= BndData_.Vel.GetBndSeg(face.GetBndIdx()).GetBndFun();
			WallVel.assign(tet, bary, bf, t);
			for(Uint i=0; i<6; ++i)
				phi[i][unknownIdx[i]] = 1;
				
			for(Uint i=0; i<6; ++i){
					locP2[i].assign(phi[i], bary);
			}

			for(Uint i=0; i<6; ++i){//setup right hand side	
					Quad5_2DCL<Point3DCL> WallVelRhs( locP2[i]* WallVel);
					loc_b[unknownIdx[i]] += (beta1_ * WallVelRhs.quad(absdet) - beta1_ * SMatrixCL<3,3> (outer_product(normal, normal)) * WallVelRhs.quad(absdet));
			}		
		}
	}	
}


//For the case beta_L =/= 0
void SpecialBndHandler_System1TwoPhaseP2CL::CLdiss(const TetraCL& tet, LocalSystem1DataCL& loc)
{
    bool SpeBnd=false;
    for(Uint v=0; v<4; v++)
    if(lsetbnd_.GetBC(*tet.GetFace(v))==Slip0BC||lsetbnd_.GetBC(*tet.GetFace(v))==SlipBC)
    {
        SpeBnd=true;
        break;
    }
	if(!SpeBnd)
	{
		for(Uint v=0; v<6; v++)
			if(lsetbnd_.GetBC(*tet.GetEdge(v))==Slip0BC||lsetbnd_.GetBC(*tet.GetEdge(v))==SlipBC)
			{
				SpeBnd=true;
				break;
			}
		if(!SpeBnd)
		return;
	}
    //Initialize one interface patch
    triangle.BInit( tet, Phi_,lsetbnd_); 
    triangle.SetBndOutNormal(outnormal_);
    Point3DCL normal_mcl[5];     //normal of moving contact lines in tangential surface
	Point3DCL outnormalOnMcl[5];    //outnormal of the domain boundary
	BaryCoordCL quadBarys[5];
	double weight[5]={0.568888889, 0.47862867,0.47862867,0.236926885,0.236926885};
	//integral in [-1,1]
	double qupt[5]={0,-0.53846931,0.53846931,-0.906179846,0.906179846};
    
    SMatrixCL<3, 3> dm[10][10];
    LocalP2CL<double> phi[10]; 
    for(Uint i=0; i<10; ++i)
    {
        phi[i][i] = 1;
    }
    for (int ch=0; ch<8; ++ch)
    {
        if (!triangle.ComputeMCLForChild(ch)) // no  for this child
            continue;
        BaryCoordCL Barys[2];
        Point3DCL pt0,pt1;
        Point3DCL midpt;
        double length;
        Uint ncl=triangle.GetNumMCL();
        for(Uint i=0;i<ncl;i++)
        {
            length = triangle.GetInfoMCL(i,Barys[0],Barys[1],pt0,pt1);
            for(Uint j=0;j<5;j++)
            {
                midpt=(pt0+pt1)/2 + qupt[j]*(pt1-pt0)/2;
                normal_mcl[j] = triangle.GetImprovedMCLNormal(i,(qupt[j]+1)/2);
                quadBarys[j]=(Barys[0]+Barys[1])/2+qupt[j]*(Barys[1]-Barys[0])/2;
                outnormalOnMcl[j]=outnormal_(midpt,0);
            }
            for (Uint i=0; i<10; ++i)
            {
                for(Uint j=0; j<=i; ++j)
                {
                    //5 points Gauss–Legendre quadrature is used.
                    for(int m =0; m<3; m++){
                        for(int n=0; n<3; n++){
                            for (int v=0; v<5; v++)
                            {
                                dm[j][i](m, n) += weight[v]*phi[i](quadBarys[v])*phi[j](quadBarys[v])*normal_mcl[v][m]*normal_mcl[v][n];      
                            }
                        }
                    }
                    dm[j][i] = 0.5*length*betaL_*dm[j][i];
                    loc.Ak[j][i] += dm[j][i];
                    if (i != j){
                        assign_transpose( dm[i][j], dm[j][i]);
                        loc.Ak[i][j] += dm[i][j];
                    }	
                }
            }
        }
    }
}

/// \brief Setup of the local P2 "system 1" on a tetra intersected by the dividing surface.
class LocalSystem1TwoPhase_P2CL
{
  private:
    const PrincipalLatticeCL& lat;

    const double mu_p, mu_n;
    const double rho_p, rho_n;
    instat_vector_fun_ptr rhs_func;

    LocalP1CL<Point3DCL> GradRefLP1[10], GradLP1[10];
    LocalP2CL<> p2;

    std::valarray<double> ls_loc;
    TetraPartitionCL partition;
    QuadDomainCL q2dom;
    QuadDomainCL q5dom;
    GridFunctionCL<double>    q[10];
    GridFunctionCL<Point3DCL> qA[10];
    GridFunctionCL<Point3DCL> rhs;

  public:
    LocalSystem1TwoPhase_P2CL (double mup, double mun, double rhop, double rhon, instat_vector_fun_ptr rhsFunc)
        : lat( PrincipalLatticeCL::instance( 2)), mu_p( mup), mu_n( mun), rho_p( rhop), rho_n( rhon), rhs_func(rhsFunc), ls_loc( lat.vertex_size())
    { P2DiscCL::GetGradientsOnRef( GradRefLP1); }

    double mu  (int sign) const { return sign > 0 ? mu_p  : mu_n; }
    double rho (int sign) const { return sign > 0 ? rho_p : rho_n; }

    void setup (const SMatrixCL<3,3>& T, double absdet, const TetraCL& tet, const LocalP2CL<>& ls, double t, LocalIntegrals_P2CL[2], LocalSystem1DataCL& loc);
};

void LocalSystem1TwoPhase_P2CL::setup (const SMatrixCL<3,3>& T, double absdet, const TetraCL& tet, const LocalP2CL<>& ls, double t, LocalIntegrals_P2CL locInt[2], LocalSystem1DataCL& loc)
{
    P2DiscCL::GetGradients( GradLP1, GradRefLP1, T);

    evaluate_on_vertexes( ls, lat, Addr( ls_loc));
    partition.make_partition<SortedVertexPolicyCL, MergeCutPolicyCL>( lat, ls_loc);
    make_CompositeQuad5Domain( q5dom, partition);
    make_CompositeQuad2Domain( q2dom, partition);
    resize_and_evaluate_on_vertexes( rhs_func, tet, q5dom, /*time*/ t, rhs);

    for (int i= 0; i < 10; ++i) {
        p2[i]= 1.; p2[i==0 ? 9 : i - 1]= 0.;
        resize_and_evaluate_on_vertexes( p2,         q5dom, q[i]); // for M
        resize_and_evaluate_on_vertexes( GradLP1[i], q2dom, qA[i]); // for A
        quad( q[i]*rhs, absdet, q5dom, locInt[0].rhs[i], locInt[1].rhs[i]); // for rhs
        quad( q[i], absdet, q5dom, locInt[0].phi[i], locInt[1].phi[i]); // for rho_phi
        loc.rho_phi[i]= rho_n*locInt[0].phi[i] + rho_p*locInt[1].phi[i];
    }
    for (int i= 0; i < 10; ++i) {
        for (int j= 0; j <= i; ++j) {
            quad( q[i]*q[j], absdet, q5dom, locInt[0].mass[i][j], locInt[1].mass[i][j]);
            quad( OuterProductExpressionCL( qA[i], qA[j]), absdet, q2dom, locInt[0].cAk[i][j], locInt[1].cAk[i][j]);
            loc.M[j][i]= rho_n*locInt[0].mass[i][j] + rho_p*locInt[1].mass[i][j];
            loc.Ak[j][i]= mu_n*locInt[0].cAk[i][j]  +  mu_p*locInt[1].cAk[i][j];
            // dot-product of the gradients
            loc.A[j][i]= trace( loc.Ak[j][i]);
            if (i != j) { // The local stiffness matrices coupM, coupA, coupAk are symmetric.
                loc.M[i][j]= loc.M[j][i];
                loc.A[i][j]= loc.A[j][i];
                assign_transpose( loc.Ak[i][j], loc.Ak[j][i]);
            }
        }
    }
}

/// \brief Setup of the local P2-times-P2X "system 1" on a tetra intersected by the dividing surface.
class LocalSystem1TwoPhase_P2XCL
{
  private:
    double mu[2], rho[2]; ///< viscosity/density on neg./pos. part

  public:
    bool supp_pos[10];          ///< store whether extended basis function has support on positive part

    LocalSystem1TwoPhase_P2XCL ( double mup, double mun, double rhop, double rhon)
        { mu[0]= mun; mu[1]= mup; rho[0]= rhon; rho[1]= rhop; }
    void setup (const LocalP2CL<>& ls, const LocalIntegrals_P2CL[2], LocalSystem1DataCL locX[2]);
};

void LocalSystem1TwoPhase_P2XCL::setup (const LocalP2CL<>& ls, const LocalIntegrals_P2CL locInt[2], LocalSystem1DataCL locX[2])
/** Assemble standard x extended part of system 1 (A, M).
 *
 *  We assume that LocalSystem1TwoPhase_P2CL::setup(...) has been called before
 *  such that members of this class can be used for discretization.
 */
{
    for (int i= 0; i < 10; ++i) {
        supp_pos[i]= ls[i] < 0;
        for (int sgn=0; sgn<2; ++sgn) // loop over neg/pos part
            locX[sgn].rho_phi[i]= rho[sgn]*locInt[sgn].phi[i];
    }
    for (int i= 0; i < 10; ++i) {
        for (int j= 0; j <= i; ++j) {
            for (int sgn=0; sgn<2; ++sgn) { // loop over neg/pos part
                locX[sgn].M[j][i]= rho[sgn]*locInt[sgn].mass[i][j];
                locX[sgn].Ak[j][i]= mu[sgn]*locInt[sgn].cAk[i][j];
            }
        }
    }
    for (int i= 0; i < 10; ++i) {
        for (int j= 0; j <= i; ++j) {
            for (int sgn=0; sgn<2; ++sgn) { // loop over neg/pos part
                // dot-product of the gradients
                locX[sgn].A[j][i]= trace( locX[sgn].Ak[j][i]);
                if (i != j) { // the local stiffness matrices M, A, Ak are symmetric.
                    locX[sgn].M[i][j]= locX[sgn].M[j][i];
                    locX[sgn].A[i][j]= locX[sgn].A[j][i];
                    assign_transpose( locX[sgn].Ak[i][j], locX[sgn].Ak[j][i]);
                }
            }
        }
    }
}

/// \brief P2 FE accumulator to set up the matrices A, M and, if requested the right-hand side b and cplM, cplA for two-phase flow.
class System1Accumulator_P2CL : public TetraAccumulatorCL
{
  protected:
    const TwoPhaseFlowCoeffCL& Coeff;
    const StokesBndDataCL& BndData;
    const VecDescCL& lset_Phi;
    const BndDataCL<double>& lset_Bnd;
    double t;

    IdxDescCL& RowIdx;
    MatrixCL& A;
    MatrixCL& M;
    VecDescCL* cplA;
    VecDescCL* cplM;
    VecDescCL* b;

    SparseMatBuilderCL<double, SMatrixCL<3,3> >* mA_;
    SparseMatBuilderCL<double, SDiagMatrixCL<3> >* mM_;

    LocalSystem1OnePhase_P2CL local_onephase; ///< used on tetras in a single phase
    LocalSystem1TwoPhase_P2CL local_twophase; ///< used on intersected tetras
    LocalSystem1DataCL loc;                   ///< Contains the memory, in which the local operators are set up; former coupM, coupA, coupAk, rho_phi.
    LocalIntegrals_P2CL locInt[2];            ///< stores computed integrals on neg./pos. part to be used by P2X discretization
	SpecialBndHandler_System1OnePhaseP2CL speBndHandler1;
	SpecialBndHandler_System1TwoPhaseP2CL speBndHandler2;

    LocalNumbP2CL n; ///< global numbering of the P2-unknowns

    SMatrixCL<3,3> T;
    double det, absdet;
    LocalP2CL<> ls_loc;
	bool speBnd;        //if there is a slip or symmetric boundary condtion

    Quad2CL<Point3DCL> rhs;
    Point3DCL loc_b[10], dirichlet_val[10]; ///< Used to transfer boundary-values from local_setup() update_global_system().

    ///\brief Computes the mapping from local to global data "n", the local matrices in loc and, if required, the Dirichlet-values needed to eliminate the boundary-dof from the global system.
    void local_setup (const TetraCL& tet);
    ///\brief Update the global system.
    void update_global_system ();

  public:
    System1Accumulator_P2CL (const TwoPhaseFlowCoeffCL& Coeff, const StokesBndDataCL& BndData_,
        const VecDescCL& ls, const BndDataCL<double>& ls_bnd, IdxDescCL& RowIdx_, MatrixCL& A_, MatrixCL& M_,
        VecDescCL* b_, VecDescCL* cplA_, VecDescCL* cplM_, double t);

    ///\brief Initializes matrix-builders and load-vectors
    void begin_accumulation ();
    ///\brief Builds the matrices
    void finalize_accumulation();

    void visit (const TetraCL& sit);

    TetraAccumulatorCL* clone (int /*tid*/) { return new System1Accumulator_P2CL ( *this); }
};

System1Accumulator_P2CL::System1Accumulator_P2CL (const TwoPhaseFlowCoeffCL& Coeff_, const StokesBndDataCL& BndData_,
    const VecDescCL& lset_arg, const BndDataCL<double>& lset_bnd, IdxDescCL& RowIdx_, MatrixCL& A_, MatrixCL& M_,
    VecDescCL* b_, VecDescCL* cplA_, VecDescCL* cplM_, double t_)
    : Coeff( Coeff_), BndData( BndData_), lset_Phi( lset_arg), lset_Bnd( lset_bnd), t( t_),
      RowIdx( RowIdx_), A( A_), M( M_), cplA( cplA_), cplM( cplM_), b( b_),
      local_twophase( Coeff.mu( 1.0), Coeff.mu( -1.0), Coeff.rho( 1.0), Coeff.rho( -1.0), Coeff.volforce),
	  speBndHandler1(BndData_, Coeff.alpha),
	  speBndHandler2(BndData_, lset_Phi, lset_Bnd, Coeff.Bndoutnormal, Coeff.mu( 1.0), Coeff.mu( -1.0), Coeff.beta(1.0), Coeff.beta(-1.0), Coeff.betaL, Coeff.alpha)
{}

void System1Accumulator_P2CL::begin_accumulation ()
{
    std::cout << "entering SetupSystem1_P2CL:\n";
    const size_t num_unks_vel= RowIdx.NumUnknowns();
    mA_= new SparseMatBuilderCL<double, SMatrixCL<3,3> >( &A, num_unks_vel, num_unks_vel);
    mM_= new SparseMatBuilderCL<double, SDiagMatrixCL<3> >( &M, num_unks_vel, num_unks_vel);
    if (b != 0) {
        b->Clear( t);
        cplM->Clear( t);
        cplA->Clear( t);
    }
}

void System1Accumulator_P2CL::finalize_accumulation ()
{
    mA_->Build();
    delete mA_;
    mM_->Build();
    delete mM_;
#ifndef _PAR
    std::cout << A.num_nonzeros() << " nonzeros in A, "
              << M.num_nonzeros() << " nonzeros in M!";
#endif
    std::cout << '\n';
}

void System1Accumulator_P2CL::visit (const TetraCL& tet)
{
    local_setup( tet);
    update_global_system();
}

void System1Accumulator_P2CL::local_setup (const TetraCL& tet)
{
    GetTrafoTr( T, det, tet);
    absdet= std::fabs( det);

    n.assign( tet, RowIdx, BndData.Vel);

    ls_loc.assign( tet, lset_Phi, lset_Bnd);
    const bool noCut= equal_signs( ls_loc);
	speBnd = false;
	//if speBnd = true , there is at least one slip or symmetric boundary on this tetra 
	for(int i =0; i< 4; ++i){

		if( BndData.Vel.GetBC(*tet.GetFace(i))==Slip0BC || BndData.Vel.GetBC(*tet.GetFace(i))==SlipBC || BndData.Vel.GetBC(*tet.GetFace(i))==SymmBC)
		{
			speBnd = true;
			break;
		}
	}
    if (noCut) {
        local_onephase.mu(  local_twophase.mu(  sign( ls_loc[0])));
        local_onephase.rho( local_twophase.rho( sign( ls_loc[0])));
        local_onephase.setup( T, absdet, loc);
		if(speBnd){
		   speBndHandler1.setMu(Coeff.mu(sign( ls_loc[0])));
		   speBndHandler1.setBeta(Coeff.beta(sign( ls_loc[0])));
		   speBndHandler1.setup(tet, T, loc);         //update loc for special boundary condtion
		}
    }
    else{
        local_twophase.setup( T, absdet, tet, ls_loc, t, locInt, loc);
        if(speBnd){
            speBndHandler2.setup(tet, T, ls_loc, loc); //update loc for special boundary condtion
            speBndHandler2.CLdiss(tet, loc);
        }
    }
		
    add_transpose_kronecker_id( loc.Ak, loc.A);

    if (b != 0) {

        if (noCut)
            rhs.assign( tet, Coeff.volforce, t);
        for (int i= 0; i < 10; ++i) {
            if (!n.WithUnknowns( i)) {
                typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
                bnd_val_fun bf= BndData.Vel.GetBndSeg( n.bndnum[i]).GetBndFun();
                dirichlet_val[i]= i<4 ? bf( tet.GetVertex( i)->GetCoord(), t)
                    : bf( GetBaryCenter( *tet.GetEdge( i-4)), t);
            }
            else { // setup b
                loc_b[i]= loc.rho_phi[i]*Coeff.g;
                if (noCut){
					loc_b[i]+= rhs.quadP2( i, absdet);
				}
                else
                    loc_b[i]+= locInt[0].rhs[i] + locInt[1].rhs[i];
            }
							
        }
        speBnd = false;
        for(int i =0; i< 4; ++i){
        	if(  BndData.Vel.GetBC(*tet.GetFace(i))==SlipBC )
        	{
        			speBnd = true;
        			break;
        	}
        }
        if(speBnd)
        {
        	if (noCut)
				speBndHandler1.setupRhs(tet, loc_b, t);
           else
		    	speBndHandler2.setupRhs(tet, loc_b, t);//<not used for now --need modification when the slip length is different in the two phase flow
        }
    }
	
}

void System1Accumulator_P2CL::update_global_system ()
{
    SparseMatBuilderCL<double, SMatrixCL<3,3> >& mA= *mA_;
    SparseMatBuilderCL<double, SDiagMatrixCL<3> >& mM= *mM_;

    for(int i= 0; i < 10; ++i){    // assemble row Numb[i]
        if (n.WithUnknowns( i)) { // dof i is not on a Dirichlet boundary
            for(int j= 0; j < 10; ++j) {
                if (n.WithUnknowns( j)) { // dof j is not on a Dirichlet boundary
                    mA( n.num[i], n.num[j])+= loc.Ak[i][j];
                    mM( n.num[i], n.num[j])+= SDiagMatrixCL<3>( loc.M[j][i]);
                }
                else if (b != 0) { // right-hand side for eliminated Dirichlet-values
                    add_to_global_vector( cplA->Data, -loc.Ak[i][j]*dirichlet_val[j], n.num[i]);
                    add_to_global_vector( cplM->Data, -loc.M[j][i] *dirichlet_val[j], n.num[i]);
                }
            }
            if (b != 0) // assemble the right-hand side
			{
                   add_to_global_vector( b->Data, loc_b[i], n.num[i]);
			}
       }
	   
	}
}

/// \brief P2X FE accumulator to set up the matrices A, M and, if requested the right-hand side b and cplM, cplA for two-phase flow.
class System1Accumulator_P2XCL : public System1Accumulator_P2CL
{
  private:
    typedef System1Accumulator_P2CL base;

    LocalNumbP2CL               nx;              ///< extended numbering of P2 dofs
    Point3DCL                   locX_b[10];      ///< storing the extended local part of b
    LocalSystem1DataCL          locX[2];         ///< storing the (extended x non-extended) local part of matrices on pos/neg part of tetra
    LocalSystem1TwoPhase_P2XCL  localX_twophase; ///< set up extended part

    ///\brief Computes the mapping from local to global data "nx" and the local matrices in locX.
    void local_setup (const TetraCL& tet);
    ///\brief Update the global system.
    void update_global_system ();

  public:
    System1Accumulator_P2XCL (const TwoPhaseFlowCoeffCL& Coeff, const StokesBndDataCL& BndData,
        const VecDescCL& ls_phi, const BndDataCL<double>& ls_bnd, IdxDescCL& RowIdx, MatrixCL& A, MatrixCL& M,
        VecDescCL* b, VecDescCL* cplA, VecDescCL* cplM, double t)
      : base(Coeff, BndData, ls_phi, ls_bnd, RowIdx, A, M, b, cplA, cplM, t),
        localX_twophase( Coeff.mu( 1.0), Coeff.mu( -1.0), Coeff.rho( 1.0), Coeff.rho( -1.0)) {}

    ///\brief Initializes matrix-builders and load-vectors
    void begin_accumulation () { base::begin_accumulation(); }
    ///\brief Builds the matrices
    void finalize_accumulation() { base::finalize_accumulation(); }

    void visit (const TetraCL& sit);

    TetraAccumulatorCL* clone (int /*tid*/) { return new System1Accumulator_P2XCL ( *this); }
};

void System1Accumulator_P2XCL::visit (const TetraCL& tet)
{
    local_setup( tet);
    update_global_system();
}

void System1Accumulator_P2XCL::local_setup (const TetraCL& tet)
{
    base::local_setup( tet);
    // ls_loc already set in base::local_setup(...)
    if (!equal_signs( ls_loc))
        localX_twophase.setup( ls_loc, locInt, locX);
    for (int sgn=0; sgn<2; ++sgn) // loop over neg/pos part
        add_transpose_kronecker_id( locX[sgn].Ak, locX[sgn].A);

    if (b != 0) {
        std::valarray<double> rhs5;
        for (int i= 0; i < 10; ++i)
            if (nx.WithUnknowns( i)) {
                // setup b for extended dofs
                const bool supp= localX_twophase.supp_pos[i]; // support on neg. or pos. part
                locX_b[i]= locInt[supp].rhs[i] + locX[supp].rho_phi[i]*Coeff.g;
            }
    }
}

void System1Accumulator_P2XCL::update_global_system ()
/** Having 10 standard P2 dof "std" and up to 10 extended dof "ext", we have to assemble three parts of the matrix:
 *  std x ext, ext x std and ext x ext.
 */
{
    base::update_global_system();
    SparseMatBuilderCL<double, SMatrixCL<3,3> >& mA= *mA_;
    SparseMatBuilderCL<double, SDiagMatrixCL<3> >& mM= *mM_;
    const bool *supp_pos= localX_twophase.supp_pos; // store whether extended basis function has support on positive part

    for(int i= 0; i < 10; ++i) {  // assemble row n.num[i] and nx.num[i]
        if (n.WithUnknowns( i)) { // dof i is not on a Dirichlet boundary
            // assemble std x ext
            for(int j= 0; j < 10; ++j) {
                if (nx.WithUnknowns( j)) { // dof j is extended
                    mA( n.num[i], nx.num[j])+= locX[supp_pos[j]].Ak[i][j];
                    mM( n.num[i], nx.num[j])+= SDiagMatrixCL<3>( locX[supp_pos[j]].M[j][i]);
                }
            }
        }
        if (nx.WithUnknowns( i)) { // dof i is extended
            for(int j= 0; j < 10; ++j) {
                if (n.WithUnknowns( j)) { // dof j is not on a Dirichlet boundary
                    // assemble ext x std
                    mA( nx.num[i], n.num[j])+= locX[supp_pos[i]].Ak[i][j];
                    mM( nx.num[i], n.num[j])+= SDiagMatrixCL<3>( locX[supp_pos[i]].M[j][i]);
                }
                if (nx.WithUnknowns( j) && supp_pos[i]==supp_pos[j]) { // dof j is extended and shares support with extended dof i
                    // assemble ext x ext
                    mA( nx.num[i], nx.num[j])+= locX[supp_pos[j]].Ak[i][j];
                    mM( nx.num[i], nx.num[j])+= SDiagMatrixCL<3>( locX[supp_pos[j]].M[j][i]);
                }
            }
            if (b != 0) // assemble the extended right-hand side
                add_to_global_vector( b->Data, locX_b[i], n.num[i]);
       }
    }
}


void SetupSystem1_P2( const MultiGridCL& MG_, const TwoPhaseFlowCoeffCL& Coeff_, const StokesBndDataCL& BndData_, MatrixCL& A, MatrixCL& M,
                      VecDescCL* b, VecDescCL* cplA, VecDescCL* cplM, const VecDescCL& lset_phi, const BndDataCL<>& lset_bnd, IdxDescCL& RowIdx, double t)
/// Set up matrices A, M and rhs b (depending on phase bnd)
{
    // TimerCL time;
    // time.Start();
    ScopeTimerCL scope("SetupSystem1_P2");
    System1Accumulator_P2CL accu( Coeff_, BndData_, lset_phi, lset_bnd, RowIdx, A, M, b, cplA, cplM, t);
    TetraAccumulatorTupleCL accus;
    MaybeAddProgressBar(MG_, "System1(P2) Setup", accus, RowIdx.TriangLevel());    accus.push_back( &accu);
    accumulate( accus, MG_, RowIdx.TriangLevel(), RowIdx.GetMatchingFunction(), RowIdx.GetBndInfo());
    // time.Stop();
    // std::cout << "setup: " << time.GetTime() << " seconds" << std::endl;
}


void SetupSystem1_P2X( const MultiGridCL& MG_, const TwoPhaseFlowCoeffCL& Coeff_, const StokesBndDataCL& BndData_, MatrixCL& A, MatrixCL& M,
                      VecDescCL* b, VecDescCL* cplA, VecDescCL* cplM, const VecDescCL& lset_phi, const BndDataCL<double>& lset_bnd, IdxDescCL& RowIdx, double t)
/// Set up matrices A, M and rhs b (depending on phase bnd)
{
    // TimerCL time;
    // time.Start();

    System1Accumulator_P2XCL accu( Coeff_, BndData_, lset_phi, lset_bnd, RowIdx, A, M, b, cplA, cplM, t);
    TetraAccumulatorTupleCL accus;
    MaybeAddProgressBar(MG_, "System1(P2X) Setup", accus, RowIdx.TriangLevel());
    accus.push_back( &accu);
    accumulate( accus, MG_, RowIdx.TriangLevel(), RowIdx.GetMatchingFunction(), RowIdx.GetBndInfo());
    // time.Stop();
    // std::cout << "setup: " << time.GetTime() << " seconds" << std::endl;
}


void SetupSystem1_P2R( const MultiGridCL& MG_, const TwoPhaseFlowCoeffCL& Coeff_, const StokesBndDataCL& BndData_, MatrixCL& A, MatrixCL& M,
                         VecDescCL* b, VecDescCL* cplA, VecDescCL* cplM, const VecDescCL& lset_phi, const BndDataCL<double>& lset_bnd, IdxDescCL& RowIdx, double t)
/// Set up matrices A, M and rhs b (depending on phase bnd)
{
    ScopeTimerCL scope("SetupSystem1_P2R");

    const IdxT num_unks_vel= RowIdx.NumUnknowns();
    const ExtIdxDescCL xidx= RowIdx.GetXidx();

    MatrixBuilderCL mA( &A, num_unks_vel, num_unks_vel),
                    mM( &M, num_unks_vel, num_unks_vel);
    if (b != 0)
    {
        b->Clear( t);
        cplM->Clear( t);
        cplA->Clear( t);
    }

    const Uint lvl = RowIdx.TriangLevel();

    LocalNumbP2CL n;
    IdxT num[14];
#ifndef _PAR
    std::cout << "entering SetupSystem1: " << num_unks_vel << " vels.\n";
#endif

    Quad2CL<Point3DCL> Grad[10], GradRef[10], rhs;
    LocalP1CL<Point3DCL> GradRefLP1[10], GradLP1[10], gradxLP1;
    LocalP2CL<Point3DCL> GradLP2[10];
    Quad2CL<double> Ones( 1.), kreuzterm;
    const double mu_p= Coeff_.mu( 1.0),
                 mu_n= Coeff_.mu( -1.0),
                 rho_p= Coeff_.rho( 1.0),
                 rho_n= Coeff_.rho( -1.0);

    SMatrixCL<3,3> T;

    double coupA[14][14], coupAk[14][14][3][3],
           coupM[14][14], rho_phi[14];
    double det, absdet, cAp, cAn, cAkp, cAkn, intHat_p, intHat_n;
    Point3DCL tmp, intRhs[14];
    LocalP2CL<> aij_n, aij_p, akreuz_n[3][3], akreuz_p[3][3], phi_i, ones( 1.);

    P2DiscCL::GetGradientsOnRef( GradRef);
    P2DiscCL::GetGradientsOnRef( GradRefLP1);

    InterfaceTetraCL patch;
    BaryCoordCL* nodes;
    LocalP2CL<> p1[4], p2[10]; // 4 P1 and 10 P2 basis functions
    LocalP2CL<> Fabs_p, Fabs_n; // enrichment function on pos./neg. part (to be interpreted as isoP2 function)
    LocalP2CL<> p1abs_p[4][8], p1abs_n[4][8]; // extended basis functions on pos./neg. part, resp., for each of the 8 regular children
    double intpos, intneg;
    for (int k=0; k<10; ++k) {
        p2[k][k]=1.;
        if (k<4)
            p1[k][k]=1.;
        else { // set corresponding edge value of p1 hat functions of corresponding vertices
            p1[VertOfEdge(k-4,0)][k]= 0.5;
            p1[VertOfEdge(k-4,1)][k]= 0.5;
        }
    }
    Quad5CL<> q[10][48], qx_p[4][48], qx_n[4][48]; // quadrature for basis functions (there exist maximally 8*6=48 SubTetras)
    LocalP2CL<> loc_phi;

    for (MultiGridCL::const_TriangTetraIteratorCL sit = MG_.GetTriangTetraBegin(lvl), send=MG_.GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        GetTrafoTr( T, det, *sit);
        absdet= std::fabs( det);

        rhs.assign( *sit, Coeff_.volforce, t);

        // collect some information about the edges and verts of the tetra
        // and save it n.
        n.assign( *sit, RowIdx, BndData_.Vel);
        loc_phi.assign( *sit, lset_phi, lset_bnd);
        patch.Init( *sit, loc_phi);
        const bool nocut= !patch.Intersects();
        if (nocut) {
            const double mu_const= patch.GetSign( 0) == 1 ? mu_p : mu_n;
            const double rho_const= patch.GetSign( 0) == 1 ? rho_p : rho_n;

            P2DiscCL::GetGradients( Grad, GradRef, T);
            // compute all couplings between HatFunctions on edges and verts
            for (int i=0; i<10; ++i)
            {
                rho_phi[i]= rho_const*Ones.quadP2( i, absdet);
                for (int j=0; j<=i; ++j)
                {
                    // dot-product of the gradients
                    const double cA= mu_const*Quad2CL<>(dot( Grad[i], Grad[j])).quad( absdet);
                    coupA[i][j]= cA;
                    coupA[j][i]= cA;
                    // kreuzterm
                    for (int k= 0; k < 3; ++k)
                        for (int l= 0; l < 3; ++l) {
                            // kreuzterm = \int mu * (dphi_i / dx_l) * (dphi_j / dx_k)
                            for (size_t m=0; m<kreuzterm.size();  ++m)
                                kreuzterm[m]= Grad[i][m][l] * Grad[j][m][k];

                            coupAk[i][j][k][l]= mu_const*kreuzterm.quad( absdet);
                            coupAk[j][i][l][k]= coupAk[i][j][k][l];
                        }
                    // As we are not at the phase-boundary this is exact:
                    const double cM= rho_const*P2DiscCL::GetMass( j, i)*absdet;
                    coupM[i][j]= cM;
                    coupM[j][i]= cM;
                }
            }
        }
        else { // We are at the phase boundary.
            // compute all couplings between HatFunctions on edges and verts
            std::memset( coupA, 0, 14*14*sizeof( double));
            std::memset( coupAk, 0, 14*14*3*3*sizeof( double));
            std::memset( rho_phi, 0, 14*sizeof( double));
            P2DiscCL::GetGradients( GradLP1, GradRefLP1, T);
            for (int i=0; i<10; ++i)
                GradLP2[i].assign( GradLP1[i]);

            double Vol_p= 0;
            P2RidgeDiscCL::GetExtBasisOnChildren(p1abs_p, p1abs_n, loc_phi);
            for (int ch= 0; ch < 8; ++ch) {
                patch.ComputeCutForChild( ch);
                Vol_p+= patch.quad( ones, absdet, true);

                LocalP2CL<Point3DCL> gradx_n[4], gradx_p[4]; // gradients of extended basis functions
                for (int i=0; i<4; ++i) { // init gradients of extended basis functions
                    P2DiscCL::GetFuncGradient( gradxLP1, p1abs_p[i][ch], GradLP1);
                    gradx_p[i].assign(gradxLP1);
                    P2DiscCL::GetFuncGradient( gradxLP1, p1abs_n[i][ch], GradLP1);
                    gradx_n[i].assign(gradxLP1);
                }
                for (int i=0; i<10; ++i) {
                    patch.quadBothParts( intHat_p, intHat_n, p2[i], absdet);
                    rho_phi[i]+= rho_p*intHat_p + rho_n*intHat_n; // \int rho*phi_i
                }
                for (int i=0; i<4; ++i) {
                    intHat_p= patch.quad( p1abs_p[i][ch], absdet, true);
                    intHat_n= patch.quad( p1abs_n[i][ch], absdet, false);
                    rho_phi[i+10]+= rho_p*intHat_p + rho_n*intHat_n; // \int rho*phi_abs
                }
                // compute coupA, coupAk
                for (int i=0; i<14; ++i) {
                    LocalP2CL<Point3DCL> &gradi_n= i<10 ? GradLP2[i] : gradx_n[i-10],
                                         &gradi_p= i<10 ? GradLP2[i] : gradx_p[i-10];
                    for (int j=0; j<=i; ++j) {
                        LocalP2CL<Point3DCL> &gradj_n= j<10 ? GradLP2[j] : gradx_n[j-10],
                                             &gradj_p= j<10 ? GradLP2[j] : gradx_p[j-10];
                        aij_n= dot( gradi_n, gradj_n);
                        aij_p= dot( gradi_p, gradj_p);
                        for (int k= 0; k < 3; ++k)
                            for (int l= 0; l < 3; ++l) {
                                // kreuzterm = \int mu * (dphi_i / dx_l) * (dphi_j / dx_k)
                                for (size_t m= 0; m < /* #Components of akreuz[k][l] */ 10;  ++m) {
                                    akreuz_n[k][l][m]= gradi_n[m][l] * gradj_n[m][k];
                                    akreuz_p[k][l][m]= gradi_p[m][l] * gradj_p[m][k];
                                }
                            }

                        // integrate aij on positive and negative part
                        cAp= patch.quad( aij_p, absdet, true);
                        cAn= patch.quad( aij_n, absdet, false);
                        const double cA= cAp*mu_p + cAn*mu_n;
                        coupA[i][j]+= cA;
                        for (int k= 0; k < 3; ++k)
                            for (int l= 0; l < 3; ++l) {
                                // integrate akreuz on positive and negative part
                                cAkp= patch.quad( akreuz_p[k][l], absdet, true);
                                cAkn= patch.quad( akreuz_n[k][l], absdet, false);
                                const double cAk= cAkp*mu_p + cAkn*mu_n;
                                coupAk[i][j][k][l]+= cAk;
                            }
                    }
                }
            } // child loop

            // compute coupM
            patch.ComputeSubTets();
            for (Uint k=0; k<patch.GetNumTetra(); ++k)
            { // init quadrature objects for basis functions
                nodes = Quad5CL<>::TransformNodes(patch.GetTetra(k));
                const int ch= patch.GetChildIdx(k);
                for (Uint j=0; j<10; ++j)  // standard FE
                    q[j][k].assign(p2[j], nodes);
                for (Uint j=0; j<4; ++j) { // extended FE
                    qx_p[j][k].assign(p1abs_p[j][ch], nodes);
                    qx_n[j][k].assign(p1abs_n[j][ch], nodes);
                }
                delete[] nodes;
            }
            for (int i=0; i<14; ++i) {
                Quad5CL<> *qi_n= i<10 ? q[i] : qx_n[i-10],
                          *qi_p= i<10 ? q[i] : qx_p[i-10];
                for (int j=0; j<=i; ++j) {
                    // M
                    intpos = 0.;
                    intneg = 0.;
                    Quad5CL<> *qj_n= j<10 ? q[j] : qx_n[j-10],
                              *qj_p= j<10 ? q[j] : qx_p[j-10];
                    for (Uint k=0; k<patch.GetNumTetra(); k++)
                        if (k<patch.GetNumNegTetra())
                            intneg += Quad5CL<>(qi_n[k]*qj_n[k]).quad(absdet*VolFrac(patch.GetTetra(k)));
                        else
                            intpos += Quad5CL<>(qi_p[k]*qj_p[k]).quad(absdet*VolFrac(patch.GetTetra(k)));
                    coupM[i][j]= rho_p*intpos + rho_n*intneg;
                }
                if (b != 0) { // setup rhs
                    intRhs[i]= Point3DCL();
                    for (Uint k=0; k<patch.GetNumTetra(); k++) {
                        nodes= Quad5CL<>::TransformNodes(patch.GetTetra(k));
                        Quad5CL<Point3DCL> rhs5( *sit, Coeff_.volforce, t, nodes);

                        if (k<patch.GetNumNegTetra())
                            intRhs[i] += Quad5CL<Point3DCL>(qi_n[k]*rhs5).quad(absdet*VolFrac(patch.GetTetra(k)));
                        else
                            intRhs[i] += Quad5CL<Point3DCL>(qi_p[k]*rhs5).quad(absdet*VolFrac(patch.GetTetra(k)));
                        delete[] nodes;
                    }
                }
            }
        }

        for (int i=0; i<14; ++i) {
            // collect local numbering
            num[i]= i<10 ? (n.WithUnknowns(i) ? n.num[i] : NoIdx) // standard FE part
                         : (nocut || !n.WithUnknowns(i-10) ? NoIdx : xidx[n.num[i-10]]);      // extended FE part
            // copy computed entries, as local stiffness matrices coupA, coupAk, coupM are symmetric
            for (int j=0; j<i; ++j) {
                coupA[j][i]= coupA[i][j];
                coupM[j][i]= coupM[i][j];
                for (int k= 0; k < 3; ++k)
                    for (int l= 0; l < 3; ++l)
                        coupAk[j][i][l][k]= coupAk[i][j][k][l];
            }
        }

        for(int i=0; i<14; ++i) {   // assemble row Numb[i]
            const IdxT numi= num[i];
            if (numi != NoIdx)  // dof i exists
            {
                for(int j=0; j<14; ++j)
                {
                    if (num[j] != NoIdx) // dof j exists
                    {
                        const IdxT numj= num[j];
                        mA( numi,   numj  )+= coupA[j][i];
                        mA( numi+1, numj+1)+= coupA[j][i];
                        mA( numi+2, numj+2)+= coupA[j][i];
                        for (int k=0; k<3; ++k)
                            for (int l=0; l<3; ++l)
                                mA( numi+k, numj+l)+= coupAk[i][j][k][l];
                        mM( numi,   numj  )+= coupM[j][i];
                        mM( numi+1, numj+1)+= coupM[j][i];
                        mM( numi+2, numj+2)+= coupM[j][i];
                    }
                    else if (b != 0 && j<10) // put coupling on rhs
                    /// \todo Interpolation of boundary data w.r.t. extended dofs not clear
                    {
                        typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
                        bnd_val_fun bf= BndData_.Vel.GetBndSeg( n.bndnum[j]).GetBndFun();
                        tmp= j<4 ? bf( sit->GetVertex( j)->GetCoord(), t)
                                : bf( GetBaryCenter( *sit->GetEdge( j-4)), t);
                        const double cA= coupA[j][i],
                                    cM= coupM[j][i];
                        for (int k=0; k<3; ++k)
                        {
                            cplA->Data[numi+k]-= cA*tmp[k];
                            for (int l=0; l<3; ++l)
                                cplA->Data[numi+k]-= coupAk[i][j][k][l]*tmp[l];
                        }
                        cplM->Data[numi  ]-= cM*tmp[0];
                        cplM->Data[numi+1]-= cM*tmp[1];
                        cplM->Data[numi+2]-= cM*tmp[2];
                    }
                }
                if (b != 0)
                {
                    tmp= intRhs[i] + rho_phi[i]*Coeff_.g;
                    b->Data[numi  ]+= tmp[0];
                    b->Data[numi+1]+= tmp[1];
                    b->Data[numi+2]+= tmp[2];
                }
            }
        }
    }

    mA.Build();
    mM.Build();
#ifndef _PAR
    std::cout << A.num_nonzeros() << " nonzeros in A, "
              << M.num_nonzeros() << " nonzeros in M! " << std::endl;
#endif
}


void InstatStokes2PhaseP2P1CL::SetupSystem1( MLMatDescCL* A, MLMatDescCL* M, VecDescCL* b, VecDescCL* cplA, VecDescCL* cplM, const LevelsetP2CL& lset, double t) const
{
    ScopeTimerCL scope("SetupSystem1 (incl. SetupBS)");

    MLMatrixCL::iterator itA = A->Data.begin();
    MLMatrixCL::iterator itM = M->Data.begin();
    MLIdxDescCL::iterator it = A->RowIdx->begin();
    MLDataCL<VecDescCL>::const_iterator itLset = lset.MLPhi.begin();
    for (size_t lvl=0; lvl < A->Data.size(); ++lvl, ++itA, ++itM, ++it, ++itLset)
        switch (it->GetFE()) {
          case vecP2_FE:
            SetupSystem1_P2 ( MG_, Coeff_, BndData_, *itA, *itM, lvl == A->Data.size()-1 ? b : 0, cplA, cplM, lvl == A->Data.size()-1 ? lset.Phi : *itLset, lset.GetBndData(), *it, t);
            break;
          case vecP2R_FE:
            SetupSystem1_P2R( MG_, Coeff_, BndData_, *itA, *itM, lvl == A->Data.size()-1 ? b : 0, cplA, cplM, lvl == A->Data.size()-1 ? lset.Phi : *itLset, lset.GetBndData(), *it, t);
            break;
          case vecP2X_FE:
            SetupSystem1_P2X( MG_, Coeff_, BndData_, *itA, *itM, lvl == A->Data.size()-1 ? b : 0, cplA, cplM, lvl == A->Data.size()-1 ? lset.Phi : *itLset, lset.GetBndData(), *it, t);
            break;
          default:
            throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupSystem1 not implemented for this FE type");
        }
    if ( Coeff_.DilVisco>0 || Coeff_.ShearVisco>0)
    {
        VecDescCL    cplBS;
        MLMatDescCL  BS;

        BS.Data.resize( vel_idx.size());
        cplBS.SetIdx( &vel_idx);
        BS.SetIdx( &vel_idx, &vel_idx);
        BS.Data.clear();
        SetupBS( &BS, &cplBS, lset, t);
        std::cout<<"Gathering BS terms on the LHS"<<std::endl;
        MLMatrixCL mat( A->Data );
        A->Data.clear();
        A->Data.LinComb( 1.0, mat, 1.0, BS.Data);
    }

}

MLTetraAccumulatorTupleCL&
InstatStokes2PhaseP2P1CL::system1_accu (MLTetraAccumulatorTupleCL& accus, MLMatDescCL* A, MLMatDescCL* M, VecDescCL* b, VecDescCL* cplA, VecDescCL* cplM, const LevelsetP2CL& lset, double t) const
{
    MLMatrixCL::iterator                    itA= A->Data.begin();
    MLMatrixCL::iterator                    itM= M->Data.begin();
    MLIdxDescCL::iterator                    it= A->RowIdx->begin();
    MLDataCL<VecDescCL>::const_iterator itLset = lset.MLPhi.begin();
    MLTetraAccumulatorTupleCL::iterator itaccu= accus.begin();
    for (size_t lvl= 0; lvl < A->Data.size(); ++lvl, ++itA, ++itM, ++it, ++itaccu, ++itLset)
        switch (it->GetFE()) {
          case vecP2_FE:
            itaccu->push_back_acquire( new System1Accumulator_P2CL( GetCoeff(), GetBndData(), lvl == A->Data.size()-1 ? *lset.PhiC : *itLset, lset.GetBndData(),
                *it, *itA, *itM, lvl == A->Data.size() - 1 ? b : 0, cplA, cplM, t));
            break;

          default:
              throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::system1_accu: not implemented for this FE type");
        }
    return accus;
}

void SetupRhs1_P2( const MultiGridCL& MG_, const TwoPhaseFlowCoeffCL& Coeff_, const StokesBndDataCL& BndData_, VecDescCL* b, const LevelsetP2CL& lset, double t)
{
    ScopeTimerCL scope("SetupRhs1_P2");
    const Uint lvl = b->GetLevel();
    b->Clear( t);

    LocalNumbP2CL n;
    SMatrixCL<3,3> T;

    Quad2CL<Point3DCL> rhs;
    Quad2CL<double> Ones( 1.);
    LocalP2CL<> phi_i;

    const double rho_p= Coeff_.rho( 1.0),
                 rho_n= Coeff_.rho( -1.0);
    double rho_phi[10];
    double det, absdet, intHat_p, intHat_n;
    Point3DCL tmp;

    InterfaceTetraCL tetra;

    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(MG_).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(MG_).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        GetTrafoTr( T, det, *sit);
        absdet= std::fabs( det);

        rhs.assign( *sit, Coeff_.volforce, t);

        // collect some information about the edges and verts of the tetra
        // and save it n.
        n.assign( *sit, *b->RowIdx, BndData_.Vel);
        tetra.Init( *sit, *lset.PhiC, lset.GetBndData());
        const bool nocut= !tetra.Intersects();
        if (nocut) {
            const double rho_const= tetra.GetSign( 0) == 1 ? rho_p : rho_n;

            // compute all couplings between HatFunctions on edges and verts
            for (int i=0; i<10; ++i)
            {
                rho_phi[i]= rho_const*Ones.quadP2( i, absdet);
            }
        }
        else { // We are at the phase boundary.
            // compute all couplings between HatFunctions on edges and verts
            std::memset( rho_phi, 0, 10*sizeof( double));
            for (int ch= 0; ch < 8; ++ch) {
                tetra.ComputeCutForChild( ch);
                for (int i=0; i<10; ++i) {
                    // init phi_i =  i-th P2 hat function
                    phi_i[i]= 1.; phi_i[i==0 ? 9 : i-1]= 0.;
                    tetra.quadBothParts( intHat_p, intHat_n, phi_i, absdet);
                    rho_phi[i]+= rho_p*intHat_p + rho_n*intHat_n; // \int rho*phi_i
                }
            }
        }


        for(int i=0; i<10; ++i)    // assemble row Numb[i]
            if (n.WithUnknowns( i))  // vert/edge i is not on a Dirichlet boundary
            {
                tmp= rhs.quadP2( i, absdet) + rho_phi[i]*Coeff_.g;
                b->Data[n.num[i]  ]+= tmp[0];
                b->Data[n.num[i]+1]+= tmp[1];
                b->Data[n.num[i]+2]+= tmp[2];
            }


    }
}


void SetupRhs1_P2R( const MultiGridCL& MG_, const TwoPhaseFlowCoeffCL& Coeff_, const StokesBndDataCL& BndData_, VecDescCL* b, const LevelsetP2CL& lset, double t)
/// \todo proper implementation missing, yet
{
    throw DROPSErrCL("SetupRhs1_P2R(...) is buggy, aborting.");
    ScopeTimerCL scope("SetupRhs1_P2R");

    const Uint lvl = b->GetLevel();

    b->Clear( t);

    LocalNumbP2CL n;
    const IdxDescCL& RowIdx= *b->RowIdx;
    const ExtIdxDescCL xidx= RowIdx.GetXidx();

    Quad2CL<Point3DCL> rhs;
    Quad2CL<double> Ones( 1.), kreuzterm;
    const double rho_p= Coeff_.rho( 1.0),
                 rho_n= Coeff_.rho( -1.0);

    SMatrixCL<3,3> T;

    double rho_phi[14];
    double det, absdet, intHat_p, intHat_n;
    Point3DCL tmp;
    LevelsetP2CL::const_DiscSolCL ls= lset.GetSolution();
    LocalP2CL<> phi_i;

    InterfaceTetraCL patch;
    BaryCoordCL* nodes;
    LocalP2CL<> p1[4], p2[10]; // 4 P1 and 10 P2 basis functions
    LocalP2CL<> Fabs_p, Fabs_n; // enrichment function on pos./neg. part (to be interpreted as isoP2 function)
    Quad5CL<> qx_p[4][48], qx_n[4][48]; // quadrature for basis functions (there exist maximally 8*6=48 SubTetras)
    LocalP2CL<> loc_phi;

    for (int k=0; k<10; ++k) {
        p2[k][k]=1.;
        if (k<4)
            p1[k][k]=1.;
        else { // set corresponding edge value of p1 hat functions of corresponding vertices
            p1[VertOfEdge(k-4,0)][k]= 0.5;
            p1[VertOfEdge(k-4,1)][k]= 0.5;
        }
    }

    for (MultiGridCL::const_TriangTetraIteratorCL sit = MG_.GetTriangTetraBegin(lvl), send=MG_.GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        GetTrafoTr( T, det, *sit);
        absdet= std::fabs( det);

        rhs.assign( *sit, Coeff_.volforce, t);

        // collect some information about the edges and verts of the tetra
        // and save it n.
        n.assign( *sit, RowIdx, BndData_.Vel);

        loc_phi.assign( *sit, ls);
        patch.Init( *sit, loc_phi);
        const bool nocut= !patch.Intersects();
        if (nocut) {
            const double rho_const= patch.GetSign( 0) == 1 ? rho_p : rho_n;

            for(int i=0; i<10; ++i)    // init rho_phi
                rho_phi[i]= rho_const*Ones.quadP2( i, absdet);
        }
        else { // We are at the phase boundary.
            for (int i= 0; i < 10; ++i) {
                // init enrichment function (to be interpreted as isoP2 function)
                Fabs_p[i]= patch.GetSign(i)== 1 ? 0 : -2*loc_phi[i];
                Fabs_n[i]= patch.GetSign(i)==-1 ? 0 :  2*loc_phi[i];
            }

            LocalP2CL<> p1abs_p[4][8], p1abs_n[4][8]; // extended basis functions on pos./neg. part, resp., for each of the 8 regular children
            for (int ch= 0; ch < 8; ++ch) {
                patch.ComputeCutForChild( ch);
                LocalP2CL<> extFabs_p, extFabs_n; // extension of enrichment function from child to parent
                // extend P1 values on child (as Fabs has to be interpreted as isoP2 function) to whole parent
                ExtendP1onChild( Fabs_p, ch, extFabs_p);
                ExtendP1onChild( Fabs_n, ch, extFabs_n);
                for (int i=0; i<4; ++i) { // init extended basis functions and its gradients
                    p1abs_p[i][ch]= p1[i]*extFabs_p;
                    p1abs_n[i][ch]= p1[i]*extFabs_n;
                }
                for (int i=0; i<10; ++i) {
                    // init phi_i =  i-th P2 hat function
                    phi_i[i]= 1.; phi_i[i==0 ? 9 : i-1]= 0.;
                    patch.quadBothParts( intHat_p, intHat_n, phi_i, absdet);
                    rho_phi[i]+= rho_p*intHat_p + rho_n*intHat_n; // \int rho*phi_i
                }
                for (int i=0; i<4; ++i) {
                    intHat_p= patch.quad( p1abs_p[i][ch], absdet, true);
                    intHat_n= patch.quad( p1abs_n[i][ch], absdet, false);
                    rho_phi[i+10]+= rho_p*intHat_p + rho_n*intHat_n; // \int rho*phi_abs
                }
            }
            patch.ComputeSubTets();
            for (Uint k=0; k<patch.GetNumTetra(); ++k)
            { // init quadrature objects for basis functions
                nodes = Quad5CL<>::TransformNodes(patch.GetTetra(k));
                const int ch= patch.GetChildIdx(k);
                for (Uint j=0; j<4; ++j) { // extended FE
                    qx_p[j][k].assign(p1abs_p[j][ch], nodes);
                    qx_n[j][k].assign(p1abs_n[j][ch], nodes);
                }
                delete[] nodes;
            }
        }

        for(int i=0; i<10; ++i)    // assemble row Numb[i]
            if (n.WithUnknowns( i))  // vert/edge i is not on a Dirichlet boundary
            {
                tmp= rhs.quadP2( i, absdet) + rho_phi[i]*Coeff_.g;
                b->Data[n.num[i]  ]+= tmp[0];
                b->Data[n.num[i]+1]+= tmp[1];
                b->Data[n.num[i]+2]+= tmp[2];

                if (i<4 && !nocut) // extended dof
                {
                    Point3DCL intRhs;
                    for (Uint k=0; k<patch.GetNumTetra(); k++) {
                        nodes= Quad5CL<Point3DCL>::TransformNodes(patch.GetTetra(k));
                        Quad5CL<Point3DCL> rhs5( *sit, Coeff_.volforce, t, nodes);
                        if (k<patch.GetNumNegTetra())
                            intRhs += Quad5CL<Point3DCL>(qx_n[i][k]*rhs5).quad(absdet*VolFrac(patch.GetTetra(k)));
                        else
                            intRhs += Quad5CL<Point3DCL>(qx_p[i][k]*rhs5).quad(absdet*VolFrac(patch.GetTetra(k)));
                        delete[] nodes;
                    }

                    tmp= intRhs + rho_phi[i+10]*Coeff_.g;
                    const IdxT xnum= xidx[n.num[i]];
                    b->Data[xnum  ]+= tmp[0];
                    b->Data[xnum+1]+= tmp[1];
                    b->Data[xnum+2]+= tmp[2];
                }
            }
    }
}


void InstatStokes2PhaseP2P1CL::SetupRhs1( VecDescCL* b, const LevelsetP2CL& lset, double t) const
/// Set up rhs b (depending on phase bnd)
{
    const FiniteElementT fe= b->RowIdx->GetFE();
    if (fe==vecP2_FE)
        SetupRhs1_P2 ( MG_, Coeff_, BndData_, b, lset, t);
    else if (fe==vecP2R_FE)
        SetupRhs1_P2R( MG_, Coeff_, BndData_, b, lset, t);
    else
        throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupRhs1 not implemented for this FE type");
}


// -----------------------------------------------------------------------------
//                        Routines for Setup Laplace-Beltrami
// -----------------------------------------------------------------------------


/// \brief Setup of the local "LB" on a tetra intersected by the dividing surface.
class LocalLBTwoPhase_P2CL
{
  private:
    const PrincipalLatticeCL& lat;
    LocalP1CL<Point3DCL> GradRefLP1[10], GradLP1[10];
    GridFunctionCL<Point3DCL> qnormal;
    GridFunctionCL<Point3DCL> qgrad[10];

    QuadDomain2DCL  q2Ddomain;
    std::valarray<double> ls_loc;
    SurfacePatchCL spatch;


    double surfTension_;
    void Get_Normals(const LocalP2CL<>& ls, LocalP1CL<Point3DCL>&);

  public:
    LocalLBTwoPhase_P2CL (double surfTension)
        : lat( PrincipalLatticeCL::instance( 2)), ls_loc( lat.vertex_size()), surfTension_( surfTension)
    { P2DiscCL::GetGradientsOnRef( GradRefLP1); }

    //Setup-Routine of (improved) LB for the tetrahedra tet
    void setup (const SMatrixCL<3,3>& T, const LocalP2CL<>& ls, const TetraCL& tet, double A[10][10]);

};

// The P2 levelset-function is used to compute the normals which are needed for the (improved) projection onto the interface, GradLP1 has to be set before
void LocalLBTwoPhase_P2CL::Get_Normals(const LocalP2CL<>& ls, LocalP1CL<Point3DCL>& Normals)
{
    for(int i=0; i<10 ; ++i)
    {
        Normals+=ls[i]*GradLP1[i];
    }

}

void LocalLBTwoPhase_P2CL::setup (const SMatrixCL<3,3>& T, const LocalP2CL<>& ls, const TetraCL& tet, double A[10][10])
{
    P2DiscCL::GetGradients( GradLP1, GradRefLP1, T);
    evaluate_on_vertexes( ls, lat, Addr( ls_loc));
    spatch.make_patch<MergeCutPolicyCL>( lat, ls_loc);
    // The routine takes the information about the tetrahedra and the cutting surface and generates a two-dimensional triangulation of the cut, including the necessary point-positions and weights for the quadrature
    make_CompositeQuad5Domain2D ( q2Ddomain, spatch, tet);
    LocalP1CL<Point3DCL> Normals;
    Get_Normals(ls, Normals);
    // Resize and evaluate Normals at all points which are needed for the two-dimensional quadrature-rule
    resize_and_evaluate_on_vertexes (Normals, q2Ddomain, qnormal);
    // Scale Normals accordingly to the Euclidean Norm (only consider the ones which make a contribution in the sense of them being big enough... otherwise one has to expect problems with division through small numbers)
    for(Uint i=0; i<qnormal.size(); ++i) {
         //if(qnormal[i].norm()> 1e-8)
         qnormal[i]= qnormal[i]/qnormal[i].norm();
    }
    // Resize and evaluate of all the 10 Gradient P1 Functions and apply pointwise projections   (P grad \xi_j  for j=1..10)
    for(int j=0; j<10 ;++j) {
        resize_and_evaluate_on_vertexes( GradLP1[j], q2Ddomain, qgrad[j]);
        qgrad[j]= qgrad[j] - dot( qgrad[j], qnormal)*qnormal;
    }
    // Do all combinations for (i,j) i,j=1..10 and corresponding quadrature
    for (int i=0; i < 10; ++i) {
        for (int j=0; j<=i; ++j) {
            A[j][i]= surfTension_*quad_2D( dot(qgrad[i],qgrad[j]),q2Ddomain);
            A[i][j]= A[j][i]; //symmetric matrix
        }
    }
}


/// \brief Accumulator to set up the matrices A and cplA for Laplace-Beltrami stabilization.
class LBAccumulator_P2CL : public TetraAccumulatorCL
{
  private:
    double locA [10][10];
    const TwoPhaseFlowCoeffCL& Coeff;
    const StokesBndDataCL& BndData;
    const VecDescCL& lset;
    const LsetBndDataCL& lset_bnd;
    double t;

    IdxDescCL& RowIdx;
    MatrixCL& A;
    VecDescCL* cplA;

    SparseMatBuilderCL<double, SDiagMatrixCL<3> >* mA_;

    LocalLBTwoPhase_P2CL local_twophase; ///< used on intersected tetras

    LocalNumbP2CL n; ///< global numbering of the P2-unknowns

    SMatrixCL<3,3> T;
    double det, absdet;
    LocalP2CL<> ls_loc;

    Point3DCL dirichlet_val[10];

    ///\brief Computes the mapping from local to global data "n", the local matrices in loc and, if required, the Dirichlet-values needed to eliminate the boundary-dof from the global system.
    void local_setup (const TetraCL& tet);
    ///\brief Update the global system.
    void update_global_system ();

  public:
    LBAccumulator_P2CL (const TwoPhaseFlowCoeffCL& Coeff, const StokesBndDataCL& BndData_,
        const VecDescCL& ls, const LsetBndDataCL& ls_bnd, IdxDescCL& RowIdx_, MatrixCL& A_, VecDescCL* cplA_, double t );

    ///\brief Initializes matrix-builders and load-vectors
    void begin_accumulation ();
    ///\brief Builds the matrices
    void finalize_accumulation();

    void visit (const TetraCL& sit);

    TetraAccumulatorCL* clone (int /*tid*/) { return new LBAccumulator_P2CL ( *this); };
};

LBAccumulator_P2CL::LBAccumulator_P2CL (const TwoPhaseFlowCoeffCL& Coeff_, const StokesBndDataCL& BndData_,
    const VecDescCL& lset_arg, const LsetBndDataCL& lset_bnd_arg, IdxDescCL& RowIdx_, MatrixCL& A_, VecDescCL* cplA_, double t_)
    : Coeff( Coeff_), BndData( BndData_), lset( lset_arg), lset_bnd( lset_bnd_arg), t( t_),
      RowIdx( RowIdx_), A( A_), cplA( cplA_), local_twophase( Coeff.SurfTens)
{}

void LBAccumulator_P2CL::begin_accumulation ()
{
    std::cout << "entering SetupLB: \n";
    const size_t num_unks_vel= RowIdx.NumUnknowns();
    mA_= new SparseMatBuilderCL<double, SDiagMatrixCL<3> >( &A, num_unks_vel, num_unks_vel);
    if (cplA != 0) {
        cplA->Clear( t);
    }
}

void LBAccumulator_P2CL::finalize_accumulation ()
{
    mA_->Build();
    delete mA_;
#ifndef _PAR
    std::cout << A.num_nonzeros() << " nonzeros in A_LB!\n";
#endif
    // std::cout << '\n';
}

void LBAccumulator_P2CL::visit (const TetraCL& tet)
{
    ls_loc.assign( tet, lset, lset_bnd);

    if (!equal_signs( ls_loc))
    {
        local_setup( tet);
        update_global_system();
    }

}

void LBAccumulator_P2CL::local_setup (const TetraCL& tet)
{
    GetTrafoTr( T, det, tet);

    n.assign( tet, RowIdx, BndData.Vel);
    local_twophase.setup( T, ls_loc, tet, locA);

    if(cplA != 0) {
        for (int i= 0; i < 10; ++i) {
            if (!n.WithUnknowns( i)) {
                typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
                bnd_val_fun bf= BndData.Vel.GetBndSeg( n.bndnum[i]).GetBndFun();
                dirichlet_val[i]= i<4 ? bf( tet.GetVertex( i)->GetCoord(), t)
                    : bf( GetBaryCenter( *tet.GetEdge( i-4)), t);
            }
        }
    }
}

void LBAccumulator_P2CL::update_global_system ()
{
    SparseMatBuilderCL<double, SDiagMatrixCL<3> >& mA= *mA_;

    for(int i= 0; i < 10; ++i)    // assemble row Numb[i]
        if (n.WithUnknowns( i)) { // dof i is not on a Dirichlet boundary
            for(int j= 0; j < 10; ++j) {
                if (n.WithUnknowns( j)) { // dof j is not on a Dirichlet boundary
                    mA( n.num[i], n.num[j])+= SDiagMatrixCL<3>( locA[j][i]);
                }
                else if (cplA != 0) { // right-hand side for eliminated Dirichlet-values
                    add_to_global_vector( cplA->Data, -locA[j][i] *dirichlet_val[j], n.num[i]);
                }
            }
        }
}

void SetupLB_P2( const MultiGridCL& MG_, const TwoPhaseFlowCoeffCL& Coeff_, const StokesBndDataCL& BndData_, MatrixCL& A, VelVecDescCL* cplA, const VecDescCL& lset, const LsetBndDataCL& lset_bnd, IdxDescCL& RowIdx, double t)
/// Set up the Laplace-Beltrami-matrix
{
    ScopeTimerCL scope("SetupLB_P2");
    LBAccumulator_P2CL accu( Coeff_, BndData_, lset, lset_bnd, RowIdx, A, cplA, t);
    TetraAccumulatorTupleCL accus;
    MaybeAddProgressBar(MG_, "LapBeltr(P2) Setup", accus, RowIdx.TriangLevel());
    accus.push_back( &accu);
    accumulate( accus, MG_, RowIdx.TriangLevel(), RowIdx.GetMatchingFunction(), RowIdx.GetBndInfo());
}


void InstatStokes2PhaseP2P1CL::SetupLB (MLMatDescCL* A, VecDescCL* cplA, const LevelsetP2CL& lset, double t) const
{
    MLMatrixCL::iterator  itA = A->Data.begin();
    MLIdxDescCL::iterator it  = A->RowIdx->begin();
    MLDataCL<VecDescCL>::const_iterator itLset = lset.MLPhi.begin();
    for (size_t lvl=0; lvl < A->RowIdx->size(); ++lvl, ++itA, ++it, ++itLset)
    {
       SetupLB_P2( MG_,  Coeff_, BndData_, *itA, lvl == A->Data.size()-1 ? cplA : 0, lvl == A->Data.size()-1 ? *lset.PhiC : *itLset, lset.GetBndData(), *it, t);
    }

}

// -----------------------------------------------------------------------------
//                        Routines for Setup Boussinesq-Scriven surface viscous term
// -----------------------------------------------------------------------------


/// \brief Setup of the local "BS" on a tetra intersected by the dividing surface.
class LocalBSTwoPhase_P2CL
{
  private:
    const PrincipalLatticeCL& lat;
    LocalP1CL<Point3DCL> GradRefLP1[10], GradLP1[10];
    GridFunctionCL<Point3DCL> qnormal;
    GridFunctionCL<Point3DCL> qgrad[10],qsurfgrad[10];
    GridFunctionCL<Point3DCL> qPhte;
    GridFunctionCL<double> qPhte_sgradv;
    Point3DCL qBnsqD, qBnsqS1, qBnsqS2;

    QuadDomain2DCL  q2Ddomain;
    std::valarray<double> ls_loc;
    SurfacePatchCL spatch;


    double surfshear_, surfdilatation_;
    void Get_Normals(const LocalP2CL<>& ls, LocalP1CL<Point3DCL>&);

  public:
    LocalBSTwoPhase_P2CL (double surfshear, double surfdilatation)
        : lat( PrincipalLatticeCL::instance( 2)), ls_loc( lat.vertex_size()), surfshear_( surfshear), surfdilatation_( surfdilatation)
    { P2DiscCL::GetGradientsOnRef( GradRefLP1); }

    //Setup-Routine of (improved) BS for the tetrahedra tet
    void setup (const SMatrixCL<3,3>& T, const LocalP2CL<>& ls, const TetraCL& tet, double A[10][10][3][3]);

};

// The P2 levelset-function is used to compute the normals which are needed for the (improved) projection onto the interface, GradLP1 has to be set before
void LocalBSTwoPhase_P2CL::Get_Normals(const LocalP2CL<>& ls, LocalP1CL<Point3DCL>& Normals)
{
    for(int i=0; i<10 ; ++i)
    {
        Normals+=ls[i]*GradLP1[i];
    }

}

void LocalBSTwoPhase_P2CL::setup (const SMatrixCL<3,3>& T, const LocalP2CL<>& ls, const TetraCL& tet, double A[10][10][3][3])
{
    P2DiscCL::GetGradients( GradLP1, GradRefLP1, T);
    evaluate_on_vertexes( ls, lat, Addr( ls_loc));
    spatch.make_patch<MergeCutPolicyCL>( lat, ls_loc);
    // The routine takes the information about the tetrahedra and the cutting surface and generates a two-dimensional triangulation of the cut, including the necessary point-positions and weights for the quadrature
    make_CompositeQuad5Domain2D ( q2Ddomain, spatch, tet);
    LocalP1CL<Point3DCL> Normals;
    Get_Normals(ls, Normals);
    // Resize and evaluate Normals at all points which are needed for the two-dimensional quadrature-rule
    resize_and_evaluate_on_vertexes (Normals, q2Ddomain, qnormal);
    // Scale Normals accordingly to the Euclidean Norm (only consider the ones which make a contribution in the sense of them being big enough... otherwise one has to expect problems with division through small numbers)
    for(Uint i=0; i<qnormal.size(); ++i) {
         //if(qnormal[i].norm()> 1e-8)
         qnormal[i]= qnormal[i]/qnormal[i].norm();
    }
    // Resize and evaluate of all the 10 Gradient P1 Functions and apply pointwise projections   (P grad \xi_j  for j=1..10)
    for(int j=0; j<10 ;++j) {
        resize_and_evaluate_on_vertexes( GradLP1[j], q2Ddomain, qgrad[j]);
        qsurfgrad[j].resize(qgrad[j].size());
        qsurfgrad[j]= qgrad[j] - dot( qgrad[j], qnormal)*qnormal;
    }
    // Compute all combinations for (i,j,k,l) i,j=1...10 k,l=1...3 and corresponding quadrature
    GridFunctionCL<Point3DCL> e[3];  //basis vectors
    for (int ei=0;ei<3;++ei)
    {
        e[ei].resize(qnormal.size());
        for (Uint i=0; i<e[ei].size(); ++i)
            e[ei][i] = std_basis<3>( ei+1);
    }

    qPhte.resize(qnormal.size());
    qPhte_sgradv.resize(qnormal.size());
    for (int k=0; k<3; ++k)
    {
        qPhte = e[k] - dot(e[k], qnormal)*qnormal;
        for (int i=0; i<10;++i)
        {
            qPhte_sgradv = dot( qPhte, qsurfgrad[i]);
            for (int j=0; j<10; ++j)
            {
                qBnsqD  = (surfdilatation_ - surfshear_)*quad_2D( qsurfgrad[j]*qPhte_sgradv, q2Ddomain);
                qBnsqS1 = surfshear_*quad_2D( qsurfgrad[i]*dot(qPhte, qgrad[j]), q2Ddomain);
                qBnsqS2 = surfshear_*quad_2D( dot(qgrad[j], qsurfgrad[i])*qPhte, q2Ddomain);
                for (int l=0; l<3; ++l)
                {
                    A[j][i][l][k] = qBnsqD[l] + qBnsqS1[l] + qBnsqS2[l];
                }
            }
        }
    }
}


/// \brief Accumulator to set up the matrices A and cplA for Boussinesq-Scriven surface viscous terms.
class BSAccumulator_P2CL : public TetraAccumulatorCL
{
  private:
    double locA [10][10][3][3];
    const TwoPhaseFlowCoeffCL& Coeff;
    const StokesBndDataCL& BndData;
    const LevelsetP2CL& lset;
    double t;

    IdxDescCL& RowIdx;
    MatrixCL& A;
    VecDescCL* cplA;

    MatrixBuilderCL* mA_;

    LocalBSTwoPhase_P2CL local_twophase; ///< used on intersected tetras

    LocalNumbP2CL n; ///< global numbering of the P2-unknowns

    SMatrixCL<3,3> T;
    double det, absdet;
    LocalP2CL<> ls_loc;

    Point3DCL dirichlet_val[10];

    ///\brief Computes the mapping from local to global data "n", the local matrices in loc and, if required, the Dirichlet-values needed to eliminate the boundary-dof from the global system.
    void local_setup (const TetraCL& tet);
    ///\brief Update the global system.
    void update_global_system ();

  public:
    BSAccumulator_P2CL (const TwoPhaseFlowCoeffCL& Coeff, const StokesBndDataCL& BndData_,
        const LevelsetP2CL& ls, IdxDescCL& RowIdx_, MatrixCL& A_, VecDescCL* cplA_, double t );

    ///\brief Initializes matrix-builders and load-vectors
    void begin_accumulation ();
    ///\brief Builds the matrices
    void finalize_accumulation();

    void visit (const TetraCL& sit);

    TetraAccumulatorCL* clone (int /*tid*/) { return new BSAccumulator_P2CL ( *this); };
};

BSAccumulator_P2CL::BSAccumulator_P2CL (const TwoPhaseFlowCoeffCL& Coeff_, const StokesBndDataCL& BndData_,
    const LevelsetP2CL& lset_arg, IdxDescCL& RowIdx_, MatrixCL& A_, VecDescCL* cplA_, double t_)
    : Coeff( Coeff_), BndData( BndData_), lset( lset_arg), t( t_),
      RowIdx( RowIdx_), A( A_), cplA( cplA_), local_twophase( Coeff.ShearVisco, Coeff.DilVisco)
{}

void BSAccumulator_P2CL::begin_accumulation ()
{
    std::cout << "entering SetupBS: \n";
    const size_t num_unks_vel= RowIdx.NumUnknowns();
    mA_= new MatrixBuilderCL( &A, num_unks_vel, num_unks_vel);
    if (cplA != 0) {
        cplA->Clear( t);
    }
}

void BSAccumulator_P2CL::finalize_accumulation ()
{
    mA_->Build();
    delete mA_;
#ifndef _PAR
    std::cout << A.num_nonzeros() << " nonzeros in A_BS!\n";
#endif
    // std::cout << '\n';
}

void BSAccumulator_P2CL::visit (const TetraCL& tet)
{
    ls_loc.assign( tet, *lset.PhiC, lset.GetBndData());

    if (!equal_signs( ls_loc))
    {
        local_setup( tet);
        update_global_system();
    }

}

void BSAccumulator_P2CL::local_setup (const TetraCL& tet)
{
    GetTrafoTr( T, det, tet);

    n.assign( tet, RowIdx, BndData.Vel);
    local_twophase.setup( T, ls_loc, tet, locA);

    if(cplA != 0) {
        for (int i= 0; i < 10; ++i) {
            if (!n.WithUnknowns( i)) {
                typedef StokesBndDataCL::VelBndDataCL::bnd_val_fun bnd_val_fun;
                bnd_val_fun bf= BndData.Vel.GetBndSeg( n.bndnum[i]).GetBndFun();
                dirichlet_val[i]= i<4 ? bf( tet.GetVertex( i)->GetCoord(), t)
                    : bf( GetBaryCenter( *tet.GetEdge( i-4)), t);
            }
        }
    }
}

void BSAccumulator_P2CL::update_global_system ()
{
    MatrixBuilderCL& mA= *mA_;

    for(int i= 0; i < 10; ++i)    // assemble row Numb[i]
        if (n.WithUnknowns( i)) { // dof i is not on a Dirichlet boundary
            for(int j= 0; j < 10; ++j) {
                if (n.WithUnknowns( j)) { // dof j is not on a Dirichlet boundary
                    for(int k=0;k<3;++k)
                        for(int l=0;l<3;++l)
                            mA( n.num[i]+k, n.num[j]+l) += locA[j][i][l][k];
                }
                else if (cplA != 0) { // right-hand side for eliminated Dirichlet-values
                    for(int k=0;k<3;++k)
                        cplA->Data[n.num[i]+k] -= (locA[j][i][0][k] + locA[j][i][1][k] + locA[j][i][2][k])*dirichlet_val[j][k];
                }
            }
        }
}

void SetupBS_P2( const MultiGridCL& MG_, const TwoPhaseFlowCoeffCL& Coeff_, const StokesBndDataCL& BndData_, MatrixCL& A, VelVecDescCL* cplA, const LevelsetP2CL& lset, IdxDescCL& RowIdx, double t)
/// Set up the Boussinesq-Scriven-matrix
{
     ScopeTimerCL scope("SetupBS_P2");
     BSAccumulator_P2CL accu( Coeff_, BndData_, lset, RowIdx, A, cplA, t);
     TetraAccumulatorTupleCL accus;
     MaybeAddProgressBar(MG_, "BousScri(P2) Setup", accus, RowIdx.TriangLevel());
     accus.push_back( &accu);
     accumulate( accus, MG_, RowIdx.TriangLevel(), RowIdx.GetMatchingFunction(), RowIdx.GetBndInfo());
}

void InstatStokes2PhaseP2P1CL::SetupBS (MLMatDescCL* A, VecDescCL* cplA, const LevelsetP2CL& lset, double t) const
{
    MLMatrixCL::iterator  itA = A->Data.begin();
    MLIdxDescCL::iterator it  = A->RowIdx->begin();
    for (size_t lvl=0; lvl < A->RowIdx->size(); ++lvl, ++itA, ++it)
    {
        SetupBS_P2( MG_,  Coeff_, BndData_, *itA, lvl == A->Data.size()-1 ? cplA : 0, lset, *it, t);
    }

}


void InstatStokes2PhaseP2P1CL::SetupSystem2( MLMatDescCL* B, VecDescCL* c, const LevelsetP2CL& lset, double t) const
// Set up matrix B and rhs c
{
    MLMatrixCL::iterator     itB   = B->Data.begin();
    MLIdxDescCL::iterator    itRow = B->RowIdx->begin();
    MLIdxDescCL::iterator    itCol = B->ColIdx->begin();
    if ( B->RowIdx->size() == 1 || B->ColIdx->size() == 1)
    { // setup B only on finest level, if row or column index has only 1 level
        itCol = B->ColIdx->GetFinestIter();
        itRow = B->RowIdx->GetFinestIter();
        itB   = B->Data.GetFinestIter();
    }
    for (; itB!=B->Data.end() && itRow!=B->RowIdx->end() && itCol!=B->ColIdx->end(); ++itB, ++itRow, ++itCol)
    {
#ifndef _PAR
        std::cout << "entering SetupSystem2: " << itRow->NumUnknowns() << " prs, " << itCol->NumUnknowns() << " vels. \n";
#endif
        VecDescCL* rhsPtr= itB==B->Data.GetFinestIter() ? c : 0; // setup rhs only on finest level
        if (itCol->GetFE()==vecP2_FE)
            switch (GetPrFE())
            {
                case P0_FE:
                    SetupSystem2_P2P0 ( MG_, Coeff_, BndData_, &(*itB), rhsPtr, &(*itRow), &(*itCol), t); break;
                case P1_FE:
                    SetupSystem2_P2P1 ( MG_, Coeff_, BndData_, &(*itB), rhsPtr, &(*itRow), &(*itCol), t); break;
                case P1X_FE:
                    SetupSystem2_P2P1X( MG_, Coeff_, BndData_, &(*itB), rhsPtr, lset, &(*itRow), &(*itCol), t); break;
                case P1D_FE:
                    SetupSystem2_P2P1D( MG_, Coeff_, BndData_, &(*itB), rhsPtr, &(*itRow), &(*itCol), t); break;
                default:
                    throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupSystem2 not implemented for this FE type");
            }
        else if (itCol->GetFE()==vecP2R_FE)
            switch (GetPrFE())
            {
                case P1_FE:
                    SetupSystem2_P2RP1 ( MG_, Coeff_, BndData_, &(*itB), rhsPtr, lset, &(*itRow), &(*itCol), t); break;
                case P1X_FE:
                    SetupSystem2_P2RP1X( MG_, Coeff_, BndData_, &(*itB), rhsPtr, lset, &(*itRow), &(*itCol), t); break;
                default:
                    throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupSystem2 not implemented for this FE type");
            }
        else
            throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupSystem2 not implemented for this FE type");
#ifndef _PAR
        std::cout << itB->num_nonzeros() << " nonzeros in B!";
#endif
        std::cout << '\n';
    }
}

MLTetraAccumulatorTupleCL&
InstatStokes2PhaseP2P1CL::system2_accu (MLTetraAccumulatorTupleCL& accus, MLMatDescCL* B, VecDescCL* c, const LevelsetP2CL& lset, double t) const
// Set up matrix B and rhs c
{
    MLMatrixCL::iterator                itB   = B->Data.begin();
    MLIdxDescCL::iterator               itRow = B->RowIdx->begin();
    MLIdxDescCL::iterator               itCol = B->ColIdx->begin();
    MLTetraAccumulatorTupleCL::iterator itaccu= accus.begin();
    if ( B->RowIdx->size() == 1 || B->ColIdx->size() == 1)
    { // setup B only on finest level, if row or column index has only 1 level
        itCol = B->ColIdx->GetFinestIter();
        itRow = B->RowIdx->GetFinestIter();
        itB   = B->Data.GetFinestIter();
        itaccu= accus.GetFinestIter();
    }
    for (; itB!=B->Data.end() && itRow!=B->RowIdx->end() && itCol!=B->ColIdx->end(); ++itB, ++itRow, ++itCol, ++itaccu)
    {
#ifndef _PAR
        std::cout << "entering SetupSystem2: " << itRow->NumUnknowns() << " prs, " << itCol->NumUnknowns() << " vels. \n";
#endif
        VecDescCL* rhsPtr= itB==B->Data.GetFinestIter() ? c : 0; // setup rhs only on finest level
        if (itCol->GetFE()==vecP2_FE)
            switch (GetPrFE()) {
                case P1_FE:
                    itaccu->push_back_acquire( new System2Accumulator_P2P1CL<TwoPhaseFlowCoeffCL>( Coeff_, BndData_, *itRow, *itCol, *itB, rhsPtr, t));
                    break;
                case P1X_FE:
                    itaccu->push_back_acquire( new System2Accumulator_P2P1XCL(Coeff_, BndData_, lset, *itRow, *itCol, *itB, rhsPtr, t));
                    break;
                default:
                    throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupSystem2 not implemented for this pressure FE type");
            }
        else
            throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::system2_accu: not implemented for this velocity FE type");
#ifndef _PAR
        std::cout << itB->num_nonzeros() << " nonzeros in B!";
#endif
        std::cout << '\n';
    }
    return accus;
}


void InstatStokes2PhaseP2P1CL::SetupRhs2( VecDescCL* c, const LevelsetP2CL& lset, double t) const
// Set up rhs c
{
    if (vel_idx.GetFinest().GetFE()==vecP2_FE)
        switch (GetPrFE())
        {
          case P0_FE:
            SetupRhs2_P2P0( MG_, Coeff_, BndData_, c, t); break;
          case P1_FE:
            SetupRhs2_P2P1( MG_, Coeff_, BndData_, c, t); break;
          case P1X_FE:
            SetupRhs2_P2P1X( MG_, Coeff_, BndData_, c, lset, t); break;
          case P1D_FE:
            SetupRhs2_P2P1D( MG_, Coeff_, BndData_, c, t); break;
          default:
            throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupRhs2 not implemented for this FE type");
        }
    else if (vel_idx.GetFinest().GetFE()==vecP2R_FE)
        switch (GetPrFE())
        {
//          case P1_FE:
//            SetupRhs2_P2RP1( MG_, Coeff_, BndData_, c, t); break;
//          case P1X_FE:
//            SetupRhs2_P2RP1X( MG_, Coeff_, BndData_, c, lset, t); break;
          default:
            throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupRhs2 not implemented for this FE type");
        }
    else
        throw DROPSErrCL("InstatStokes2PhaseP2P1CL<Coeff>::SetupRhs2 not implemented for this FE type");
}


void InstatStokes2PhaseP2P1CL::SetupBdotv (VecDescCL* Bdotv, const VelVecDescCL* vel,
    const LevelsetP2CL& lset, double t) const
{
    ScopeTimerCL scope("SetupBdotv");
    Bdotv->Clear( t);
    const Uint lvl= Bdotv->GetLevel();
    IdxT prNumb[4];

    LocalP1CL<Point3DCL>  Grad[10], GradRef[10]; // Gradient of p2-hat-functions
    P2DiscCL::GetGradientsOnRef( GradRef);
    LocalP2CL<Point3DCL>  lp2Grad;
    LocalP2CL<Point3DCL> loc_u;
    LocalP2CL<>  divu;
    Quad5_2DCL<Point3DCL> qGrad;
    Quad5_2DCL<Point3DCL> n, qu;
    Quad5_2DCL<> qdivu, q1, q2;
    LocalP1CL<double> lp1[4]; // p1-hat-functions
    for (int i= 0; i < 4; ++i) lp1[i][i]= 1.;

    SMatrixCL<3,3> T;
    double det;
    InterfaceTriangleCL cut;
    const ExtIdxDescCL& p_xidx= Bdotv->RowIdx->GetXidx();

    DROPS_FOR_TRIANG_TETRA( MG_, lvl, sit) {
        cut.Init( *sit, *lset.PhiC, lset.GetBndData());
        if (!cut.Intersects()) continue;

        GetLocalNumbP1NoBnd( prNumb, *sit, *Bdotv->RowIdx);
        GetTrafoTr( T, det, *sit);
        P2DiscCL::GetGradients( Grad, GradRef, T);
        loc_u.assign( *sit, *vel, GetBndData().Vel);
        divu= 0.;
        for (int i= 0; i < 10; ++i) {
            lp2Grad.assign( Grad[i]);
            divu+= dot( LocalP2CL<Point3DCL>( loc_u[i]), lp2Grad);
        }
        for (int ch= 0; ch < 8; ++ch) {
            cut.ComputeForChild( ch);
            for (int t= 0; t < cut.GetNumTriangles(); ++t) {
                const BaryCoordCL* const p( &cut.GetBary( t));
                qu.assign( loc_u, p);
                qdivu.assign( divu, p);
                n= Point3DCL();
                for (int v= 0; v < 10; ++v) {
                    qGrad.assign( Grad[v], p);
                    n+= cut.GetPhi( v)*qGrad;
                }
                for (int i= 0; i < Quad5_2DDataCL::NumNodesC; ++i)
                    if (n[i].norm()>1e-8) n[i]/= n[i].norm();
                q1= dot( n, qu)*qdivu;
                for(int pr= 0; pr < 4; ++pr) {
                    const IdxT xidx( p_xidx[prNumb[pr]]);
                    if (xidx == NoIdx) continue;
                    q2.assign( lp1[pr], p);
                    q2*= q1;
                    // n is the outer normal of {lset <= 0}; we need the outer normal of supp(pr-hat-function)\cap \Gamma).
                    Bdotv->Data[xidx]-= (cut.GetSign( pr) > 0 ? -1. : 1.)*q2.quad( cut.GetAbsDet( t));
                }
            }
        }
    }
}


void InstatStokes2PhaseP2P1CL::SetIdx()
{
    MLIdxDescCL* vidx= &vel_idx;
    MLIdxDescCL* pidx= &pr_idx;

    b.SetIdx   ( vidx);
    c.SetIdx   ( pidx);

    A.SetIdx   ( vidx, vidx);
    B.SetIdx   ( pidx, vidx);
    C.SetIdx   ( pidx, pidx);
    prM.SetIdx ( pidx, pidx);
    prA.SetIdx ( pidx, pidx);
    M.SetIdx   ( vidx, vidx);
}


void InstatStokes2PhaseP2P1CL::SetNumVelLvl( size_t n)
{
    match_fun match= MG_.GetBnd().GetMatchFun();
    const double bound = vel_idx.GetFinest().GetXidx().GetBound();
    vel_idx.resize( n, GetVelFE(), BndData_.Vel, match, bound);
    A.Data.resize   (vel_idx.size());
    M.Data.resize   (vel_idx.size());
}


void InstatStokes2PhaseP2P1CL::SetNumPrLvl( size_t n)
{
    match_fun match= MG_.GetBnd().GetMatchFun();
    const double bound = pr_idx.GetFinest().GetXidx().GetBound();
    pr_idx.resize( n, GetPrFE(),  BndData_.Pr, match, bound);
    B.Data.resize   (pr_idx.size());
    C.Data.resize   (pr_idx.size());
    prM.Data.resize (pr_idx.size());
    prA.Data.resize (pr_idx.size());
}


void InstatStokes2PhaseP2P1CL::GetPrOnPart( VecDescCL& p_part, const LevelsetP2CL& lset, bool posPart)
{
    const Uint lvl= p.RowIdx->TriangLevel(),
          idxnum= p.RowIdx->GetIdx();
    LevelsetP2CL::const_DiscSolCL ls= lset.GetSolution();
    const MultiGridCL& mg= this->GetMG();
    const ExtIdxDescCL& Xidx= this->GetXidx();

    p_part.SetIdx( p.RowIdx);
    VectorCL& pp= p_part.Data;
    pp= p.Data;

    // add extended pressure
    for( MultiGridCL::const_TriangVertexIteratorCL it= mg.GetTriangVertexBegin(lvl),
        end= mg.GetTriangVertexEnd(lvl); it != end; ++it)
    {
		 if (!it->Unknowns.Exist( idxnum)) continue;
        const IdxT nr= it->Unknowns(idxnum);
        if (Xidx[nr]==NoIdx) continue;

        const bool sign= InterfacePatchCL::Sign( ls.val( *it))==1;
        if (posPart==sign) continue; // extended hat function ==0 on this part
        if (posPart)
            pp[nr]+= p.Data[Xidx[nr]];
        else
            pp[nr]-= p.Data[Xidx[nr]];
    }
}

double InstatStokes2PhaseP2P1CL::GetCFLTimeRestriction( LevelsetP2CL& lset)
{
    const Uint lvl= p.RowIdx->TriangLevel();
    const MultiGridCL& mg= this->GetMG();
    const_DiscVelSolCL vel= GetVelSolution();
    LocalP2CL<Point3DCL> velLoc;
    LocalNumbP2CL curvNumb;
    VecDescCL curv( &vel_idx);
    lset.AccumulateBndIntegral( curv);

    double convMax= -1, viscMax= -1., gravMax= -1, stMax= -1;
    const double rho_min= std::min( Coeff_.rho(-1.), Coeff_.rho(1.)),
            nu_max= std::max( Coeff_.mu(-1.)/Coeff_.rho(-1.), Coeff_.mu(1.)/Coeff_.rho(1.));

    for( MultiGridCL::const_TriangTetraIteratorCL it= mg.GetTriangTetraBegin(lvl),
        end= mg.GetTriangTetraEnd(lvl); it != end; ++it)
    {
        velLoc.assign( *it, vel);
        curvNumb.assign( *it, *curv.RowIdx, BndData_.Vel);

        // compute average curvature
        double tauKappa= 0., visc= 0.,
            h_min=1e99;
        for (int i=0; i<10; ++i)
            if (curvNumb.WithUnknowns(i))
                tauKappa+= curv.Data[curvNumb.num[i]];

        tauKappa/= it->GetVolume();

        for ( TetraCL::const_EdgePIterator ed= it->GetEdgesBegin(), eend= it->GetEdgesEnd(); ed!=eend; ++ed)
        {
            const Point3DCL dir= (*ed)->GetVertex(0)->GetCoord() - (*ed)->GetVertex(1)->GetCoord();
            const double length= norm(dir);
            if (length < h_min) h_min= length;
            visc+= 1./length/length;

            const double grav= std::sqrt(std::abs( inner_prod( Coeff_.g, dir)/length/length));
            if (grav > gravMax) gravMax= grav;

            for (int i=0; i<10; ++i) {
                const double conv= std::abs( inner_prod( velLoc[i], dir)/length/length);
                if (conv > convMax) convMax= conv;
            }
        }

        visc*= nu_max;
        if (visc > viscMax) viscMax= visc;

        const double st= std::sqrt(std::abs(tauKappa)/rho_min/h_min/h_min);
        if (st > stMax) stMax= st;
    }

    const double dtMax= 2./(convMax + viscMax + std::sqrt( (convMax+viscMax)*(convMax+viscMax) + 4*gravMax*gravMax + 4*stMax*stMax));

    std::cout << "CFL factors: conv= " << convMax << "\tvisc = " << viscMax << "\tgrav = " << gravMax << "\tst = " << stMax
        << " \n\t dt < " << dtMax << std::endl;

    return dtMax;
}

void InstatStokes2PhaseP2P1CL::CheckOnePhaseSolution(const VelVecDescCL* DescVel, const VecDescCL* DescPr, 
							      const instat_vector_fun_ptr RefVel, const instat_vector_fun_ptr RefGradPr , const instat_scalar_fun_ptr RefPr) const
{

	ScopeTimerCL scope("CheckOnePhaseSolution");
    double t = DescVel->t;
//#ifdef _PAR
//    const ExchangeCL& exV = vel_idx.GetEx();
//    const ExchangeCL& exP = pr_idx.GetEx();
//#endif
    Uint lvl=DescVel->GetLevel();

    SMatrixCL<3,3> T;
    double det;
	Point3DCL Grad[4];
	
    // L2 norms of velocities: u_h - u
	// L2 norm of gradient of pressure: nabla (p_h - p)
    // number of nodes for Quad5CL rule is 15 (see discretize.h Quad5_DataCL NumNodesC =15)
    double L2_vel(0.0);
	double L2_pr(0.0);
    double Grad_pr(0.0);
    Quad5CL<Point3DCL> q5_vel, q5_vel_exact;
    Quad5CL<Point3DCL> q5_grad_pr_exact;
    Quad5CL<double>    q5_pr, q5_pr_exact;
	
	
	double SumErr=0.;
	double volume=0.;
	double err_aver=0.;
    for (MultiGridCL::const_TriangTetraIteratorCL sit= const_cast<const MultiGridCL&>(MG_).GetTriangTetraBegin(lvl),
        send= const_cast<const MultiGridCL&>(MG_).GetTriangTetraEnd(lvl); sit != send; ++sit)
    {
         GetTrafoTr(T,det,*sit);
         const double absdet= std::fabs(det);
         LocalP1CL<double> loc_pr(*sit, make_P1Eval(MG_,BndData_.Pr,*DescPr));
		 
		 volume += sit->GetVolume();
		 q5_pr_exact.assign(*sit, RefPr, 0);
		 q5_pr.assign(loc_pr);
		 SumErr += Quad5CL<> (q5_pr-q5_pr_exact).quad(absdet);			 
     }
	 //Get the average pressure, use it to normalize the pressure;
	err_aver= SumErr/volume;

    for (MultiGridCL::const_TriangTetraIteratorCL sit= const_cast<const MultiGridCL&>(MG_).GetTriangTetraBegin(lvl),
        send= const_cast<const MultiGridCL&>(MG_).GetTriangTetraEnd(lvl); sit != send; ++sit)
    {
         GetTrafoTr(T,det,*sit);
         const double absdet= std::fabs(det);
         LocalP2CL<Point3DCL> loc_vel(*sit, make_P2Eval(MG_,BndData_.Vel,*DescVel));
         LocalP1CL<double> loc_pr(*sit, make_P1Eval(MG_,BndData_.Pr,*DescPr));
		 LocalP1CL<double> loc_aver(err_aver);
		
		 Point3DCL loc_grad_pr(0.);
		 P1DiscCL::GetGradients(Grad, det, *sit);		 
		 for(int i=0; i < 4; i++)
			 loc_grad_pr += loc_pr[i] * Grad[i];  //Gradient of pressure
			 
         q5_vel.assign(loc_vel);
         q5_vel_exact.assign(*sit, RefVel,t);
		 LocalP1CL<double> temp(loc_pr-loc_aver);
		 q5_pr.assign(temp);
		 q5_pr_exact.assign(*sit, RefPr, t);
		 Quad5CL<Point3DCL> q5_grad_pr(loc_grad_pr);
		 q5_grad_pr_exact.assign(*sit, RefGradPr, t);
         Quad5CL<Point3DCL> q5_vel_diff( q5_vel-q5_vel_exact);
		 Quad5CL<Point3DCL> q5_grad_pr_diff(q5_grad_pr-q5_grad_pr_exact);
		 Quad5CL<> q5_pr_diff( q5_pr-q5_pr_exact);
         L2_vel += Quad5CL<> (dot(q5_vel_diff,q5_vel_diff)).quad(absdet);
		 Grad_pr+= Quad5CL<> (dot(q5_grad_pr_diff, q5_grad_pr_diff)).quad(absdet);
		 L2_pr+= Quad5CL<> (q5_pr_diff*q5_pr_diff).quad(absdet);
     }
     L2_vel  = std::sqrt(L2_vel);               //L2_vel is the true value.
	 Grad_pr = std::sqrt(Grad_pr);
     L2_pr   = std::sqrt(L2_pr); 	 
         std::cout << "---------------------Discretize error-------------------"
		           <<"\n || u_h - u ||_L2 = " <<  L2_vel 
				   <<"\n || p_h - p ||_L2 = " <<  L2_pr 
				   <<"\n || nabla (p_h - p) ||_L2 = " << Grad_pr << std::endl;	
}
//-------------------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------
//The three functions in this block is used to repeat the L2 pressure error checker in prJump.cpp
/*Point3DCL Radius; 
Point3DCL Mitte;
double DistanceFct( const Point3DCL& p, double)
{
	static bool first = true;
	if (first){
		Radius =  P.get<DROPS::Point3DCL>("Exp.RadDrop");
		Mitte  =  P.get<DROPS::Point3DCL>("Exp.PosDrop");
	}
	Point3DCL d= p - Mitte;
	const double avgRad= cbrt(Radius[0]*Radius[1]*Radius[2]);
	d/= Radius;
	return std::abs( avgRad)*d.norm() - avgRad;
}

void InitialPr( VecDescCL& p, double delta_p, const MultiGridCL& mg, const FiniteElementT prFE, const ExtIdxDescCL& Xidx)
{
    const Uint lvl= p.RowIdx->TriangLevel(),
        idxnum= p.RowIdx->GetIdx();

    delta_p/= 2;
    switch (prFE)
    {
      case P0_FE:
        for( MultiGridCL::const_TriangTetraIteratorCL it= mg.GetTriangTetraBegin(lvl),
            end= mg.GetTriangTetraEnd(lvl); it != end; ++it)
        {
            const double dist= DistanceFct( GetBaryCenter( *it), 0);
            p.Data[it->Unknowns(idxnum)]= dist > 0 ? -delta_p : delta_p;
        }
        break;
      case P1X_FE:
        for( MultiGridCL::const_TriangVertexIteratorCL it= mg.GetTriangVertexBegin(lvl),
            end= mg.GetTriangVertexEnd(lvl); it != end; ++it)
        {
            const IdxT idx= it->Unknowns(idxnum);
            if (Xidx[idx]==NoIdx) continue;
            p.Data[Xidx[idx]]= -2*delta_p; // jump height
        }
      case P1_FE: // and P1X_FE
        for( MultiGridCL::const_TriangVertexIteratorCL it= mg.GetTriangVertexBegin(lvl),
            end= mg.GetTriangVertexEnd(lvl); it != end; ++it)
        {
            const double dist= DistanceFct( it->GetCoord(), 0.);
            p.Data[it->Unknowns(idxnum)]= InterfacePatchCL::Sign(dist)==1 ? -delta_p : delta_p;
        }
        break;
      default:
        std::cout << "InitPr not implemented yet for this FE type!\n";
    }
}

void L2ErrPr( const VecDescCL& p, const LevelsetP2CL& lset, const MatrixCL& prM, double delta_p, const MultiGridCL& mg, const FiniteElementT prFE, const ExtIdxDescCL& Xidx, double p_ex_avg)
{
    const double min= p.Data.min(), max= p.Data.max();
    std::cout << "pressure min/max/diff:\t" << min << "\t" << max << "\t" << (max-min-delta_p) << "\n";

    VectorCL ones( 1.0, p.Data.size());
    if (prFE==P1X_FE)
        for (int i=Xidx.GetNumUnknownsStdFE(), n=ones.size(); i<n; ++i)
            ones[i]= 0;
    const double Vol= dot( prM*ones, ones)*P.get<double>("Mat.ViscDrop"); // note that prM is scaled by 1/mu !!
// std::cout << "Vol = " << Vol << '\n';
    const double p_avg= dot( prM*p.Data, ones)*P.get<double>("Mat.ViscDrop")/Vol; // note that prM is scaled by 1/mu !!
    VectorCL diff( p.Data - p_avg*ones);
    const double p0_avg= dot( prM*diff, ones)*P.get<double>("Mat.ViscDrop")/Vol;
    std::cout << "average of pressure:\t" << p_avg << std::endl;
    std::cout << "avg. of scaled pr:\t" << p0_avg << std::endl;

    if (prFE==P1X_FE)
    {
        VecDescCL p_exakt( p.RowIdx);
        InitialPr( p_exakt, delta_p, mg, prFE, Xidx);
        const double p_ex_avg2= dot( prM*p_exakt.Data, ones)*P.get<double>("Mat.ViscDrop")/Vol;
        std::cout << "avg. of exact pr:\t" << p_ex_avg2 << std::endl;
        diff-= VectorCL( p_exakt.Data - p_ex_avg*ones);
        const double L2= std::sqrt( P.get<double>("Mat.ViscDrop")*dot( prM*diff, diff));
        std::cout << "*************\n"
                  << "assuming avg(p*)==" << p_ex_avg
                  << "  ===>  \t||e_p||_L2 = " << L2 << std::endl
                  << "*************\n";
    }
}*/
//-----------------------------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------------------------

//To check solution with continuous velocity, and discountious pressure;
void InstatStokes2PhaseP2P1CL::CheckTwoPhaseSolution(const VelVecDescCL* DescVel, const VecDescCL* DescPr, 
							      const LevelsetP2CL& lset, const instat_vector_fun_ptr RefVel, const instat_scalar_fun_ptr RefPr)
{
	ScopeTimerCL scope("CheckTwoPhaseSolution");
    double t = DescVel->t;
//#ifdef _PAR
//    const ExchangeCL& exV = vel_idx.GetEx();
//    const ExchangeCL& exP = pr_idx.GetEx();
//#endif
	
	Uint lvl=DescVel->GetLevel();
	VecDescCL DescPr_neg; 
	VecDescCL DescPr_pos;
    /// \todo for periodic stuff: matching function here

    DescPr_neg.SetIdx( p.RowIdx);
    DescPr_pos.SetIdx( p.RowIdx);
	GetPrOnPart(DescPr_neg, lset, false);
	GetPrOnPart(DescPr_pos, lset, true);
	
    SMatrixCL<3,3> T;
    double det;
	//Point3DCL Grad[4];
	
    // L2 norms of velocities: ||u_h - u||_L2
	// L2 norm of  pressure:   ||p_h - p||_L2
    // number of nodes for Quad5CL rule is 15 (see discretize.h Quad5_DataCL NumNodesC =15)
    double L2_vel(0.0);
    double L2_pr(0.0);
    Quad5CL<Point3DCL> q5_vel, q5_vel_exact;
    Quad5CL<double> q5_pr, q5_pr_exact;
	GridFunctionCL<double> pre_neg, pre_pos, pre_exact1,pre_exact2, err_neg, err_pos;
	
	TetraPartitionCL partition;
	QuadDomainCL q5dom;
	InterfaceTetraCL cut;
	const PrincipalLatticeCL lat(PrincipalLatticeCL::instance(2));
    std::valarray<double> ls_loc(lat.vertex_size());
	
	
	double SumErr=0.;
	double volume=0.;
	double average=0.;
    for (MultiGridCL::const_TriangTetraIteratorCL sit= const_cast<const MultiGridCL&>(MG_).GetTriangTetraBegin(lvl),
        send= const_cast<const MultiGridCL&>(MG_).GetTriangTetraEnd(lvl); sit != send; ++sit)
    {
		 evaluate_on_vertexes( lset.GetSolution(), *sit, lat, Addr( ls_loc));
		 const bool noCut= equal_signs( ls_loc);	 
         GetTrafoTr(T,det,*sit);
         const double absdet= std::fabs(det);
         LocalP1CL<double> loc_pr(*sit, make_P1Eval(MG_,BndData_.Pr,*DescPr));
		 
		 volume += sit->GetVolume();
		 if(noCut){
			 if (sign(ls_loc[0])==1)
				 q5_pr_exact.assign(*sit, RefPr, 4);
			 else
				 q5_pr_exact.assign(*sit, RefPr, 6);
			 q5_pr.assign(loc_pr);
			 SumErr += Quad5CL<> (q5_pr-q5_pr_exact).quad(absdet);			 
		 }
		 else{
			 partition.make_partition<SortedVertexPolicyCL, MergeCutPolicyCL>( lat, ls_loc);
			 make_CompositeQuad2Domain( q5dom, partition);
			 LocalP1CL<double> loc_pr_neg(*sit, make_P1Eval(MG_,BndData_.Pr, DescPr_neg));
			 LocalP1CL<double> loc_pr_pos(*sit, make_P1Eval(MG_,BndData_.Pr, DescPr_pos));

			 resize_and_evaluate_on_vertexes( loc_pr_neg, q5dom, pre_neg);
			 resize_and_evaluate_on_vertexes( loc_pr_pos, q5dom, pre_pos); 
			 
			 resize_and_evaluate_on_vertexes( RefPr, *sit, q5dom, 6., pre_exact1);
			 resize_and_evaluate_on_vertexes( RefPr, *sit, q5dom, 4., pre_exact2); 	
			 
			 SumErr+= quad ( (pre_neg-pre_exact1), absdet, q5dom, NegTetraC);
			 SumErr+= quad ( (pre_pos-pre_exact2), absdet, q5dom, PosTetraC);	
		 }
     }
	 //Get the average pressure, use it to normalize the pressure;
	average= SumErr/volume;
	std::cout<<"The average of the pressure error is: "<<average<<" The volume is: "<<volume<<std::endl;
    for (MultiGridCL::const_TriangTetraIteratorCL sit= const_cast<const MultiGridCL&>(MG_).GetTriangTetraBegin(lvl),
        send= const_cast<const MultiGridCL&>(MG_).GetTriangTetraEnd(lvl); sit != send; ++sit)
    {
		 evaluate_on_vertexes( lset.GetSolution(), *sit, lat, Addr( ls_loc));
		 cut.Init( *sit, lset.Phi, lset.GetBndData());
		 const bool noCut= equal_signs( ls_loc);
		 	
         GetTrafoTr(T,det,*sit);
         const double absdet= std::fabs(det);
         LocalP2CL<Point3DCL> loc_vel(*sit, make_P2Eval(MG_,BndData_.Vel,*DescVel));
         LocalP1CL<double> loc_pr(*sit, make_P1Eval(MG_,BndData_.Pr,*DescPr));
		  LocalP1CL<double> loc_aver(average);
			 
         q5_vel.assign(loc_vel);
         q5_vel_exact.assign(*sit, RefVel,t);
         Quad5CL<Point3DCL> q5_vel_diff( q5_vel-q5_vel_exact);
         L2_vel += Quad5CL<> (dot(q5_vel_diff,q5_vel_diff)).quad(absdet);
		 
		 if(noCut){
			 if (sign(ls_loc[0])==1)
				 q5_pr_exact.assign(*sit, RefPr, 4);
			 else
				 q5_pr_exact.assign(*sit, RefPr, 6);
			 LocalP1CL<double> temp3(loc_pr-loc_aver);
			 q5_pr.assign(temp3);
	
			 Quad5CL<double> q5_pr_diff( q5_pr-q5_pr_exact);

			 L2_pr  += Quad5CL<> (q5_pr_diff*q5_pr_diff).quad(absdet);			 
		 }
		 else{
			 partition.make_partition<SortedVertexPolicyCL, MergeCutPolicyCL>( lat, ls_loc);
			 make_CompositeQuad2Domain( q5dom, partition);
			 LocalP1CL<double> loc_pr_neg(*sit, make_P1Eval(MG_,BndData_.Pr, DescPr_neg));
			 LocalP1CL<double> loc_pr_pos(*sit, make_P1Eval(MG_,BndData_.Pr, DescPr_pos));
			 LocalP1CL<double> temp1(loc_pr_neg-loc_aver); //for negative pressure;
			 LocalP1CL<double> temp2(loc_pr_pos-loc_aver); //for positive pressure;
			 LocalP2CL<double> pr_neg_l2 (temp1);
			 LocalP2CL<double> pr_pos_l2 (temp2);
			 LocalP2CL<double> pr_ex_neg (*sit, RefPr, 6);
			 LocalP2CL<double> pr_ex_pos (*sit, RefPr, 4);
			 LocalP2CL<double> err_neg_l2( (pr_neg_l2-pr_ex_neg)*(pr_neg_l2-pr_ex_neg) );
			 LocalP2CL<double> err_pos_l2( (pr_pos_l2-pr_ex_pos)*(pr_pos_l2-pr_ex_pos) );
			 
			 resize_and_evaluate_on_vertexes(err_neg_l2,  q5dom,  err_neg);
			 resize_and_evaluate_on_vertexes(err_pos_l2,  q5dom,  err_pos); 
			 //To do: currently not find the best way, use the time to as level set sign flag;			 
			 L2_pr += quad ( err_pos,  absdet, q5dom,   PosTetraC);
			 L2_pr += quad ( err_neg,  absdet, q5dom,   NegTetraC);	 
		 }
     }
     L2_vel  = std::sqrt(L2_vel);               //L2_vel is the true value.
     L2_pr   = std::sqrt(L2_pr);
         std::cout << "---------------------Discretize error-------------------"
		            <<"\n || u_h - u ||_L2 = " <<  L2_vel 
				     <<"\n || p_h - p ||_L2 = " <<  L2_pr << std::endl;	
	
    //The following code is repeating the L2 pressure error checker in prJump.cpp
	/*const double Vol= 8.,
        prJump= P.get<double>("SurfTens.SurfTension")*2/P.get<DROPS::Point3DCL>("Exp.RadDrop")[0], // for SF_*LB force
        avg_ex= prJump/2.*(8./3.*M_PI*P.get<DROPS::Point3DCL>("Exp.RadDrop")[0]*P.get<DROPS::Point3DCL>("Exp.RadDrop")[0]*P.get<DROPS::Point3DCL>("Exp.RadDrop")[0] - Vol)/Vol; // for spherical interface
	
	if (UsesXFEM())
    {
        const ExtIdxDescCL& Xidx= GetXidx();
        const size_t n= p.Data.size();

        const double limtol= 10,
            lim_min= -prJump - limtol*prJump,
            lim_max= -prJump + limtol*prJump;
        double xmin= 1e99, xmax= -1e99, sum= 0, sum_lim= 0;
        IdxT num= 0;
        for (size_t i=Xidx.GetNumUnknownsStdFE(); i<n; ++i)
        {
            const double pr= p.Data[i];
            sum+= pr;
            ++num;
            if (pr>xmax) xmax= pr;
            if (pr<xmin) xmin= pr;
            if (pr>lim_max) p.Data[i]= lim_max;
            if (pr<lim_min) p.Data[i]= lim_min;
            sum_lim+= p.Data[i];
        }
        std::cout << "extended pr: min/max/avg = " << xmin << ", " << xmax << ", " << sum/num << std::endl;
        std::cout << "limited pr:  min/max/avg = " << lim_min << ", " << lim_max << ", " << sum_lim/num << std::endl;
    }
	
	SetupPrMass(&prM, lset);
	L2ErrPr( p, lset, prM.Data.GetFinest(), prJump, MG_, GetPrFE(), GetXidx(), avg_ex);	*/ 
}

void InstatStokes2PhaseP2P1CL::CheckTwoPhaseSolution(const VelVecDescCL* DescVel, const VecDescCL* DescPr, 
							      const LevelsetP2CL& lset, const VelVecDescCL* RefVel, const VecDescCL* RefPr)
{
	ScopeTimerCL scope("CheckTwoPhaseSolution");
    double t = DescVel->t;
//#ifdef _PAR
//    const ExchangeCL& exV = vel_idx.GetEx();
//    const ExchangeCL& exP = pr_idx.GetEx();
//#endif
	
	Uint lvl=DescVel->GetLevel();
	VecDescCL DescPr_neg; 
	VecDescCL DescPr_pos;
    /// \todo for periodic stuff: matching function here

    DescPr_neg.SetIdx( p.RowIdx);
    DescPr_pos.SetIdx( p.RowIdx);
	GetPrOnPart(DescPr_neg, lset, false);
	GetPrOnPart(DescPr_pos, lset, true);
	
    SMatrixCL<3,3> T;
    double det;
	//Point3DCL Grad[4];
	
    // L2 norms of velocities: ||u_h - u||_L2
	// L2 norm of  pressure:   ||p_h - p||_L2
    // number of nodes for Quad5CL rule is 15 (see discretize.h Quad5_DataCL NumNodesC =15)
    double L2_vel(0.0);
    double L2_pr(0.0);
    Quad5CL<Point3DCL> q5_vel, q5_vel_exact;
    Quad5CL<double> q5_pr, q5_pr_exact;
	GridFunctionCL<double> pre_neg, pre_pos, pre_exact1,pre_exact2, err_neg, err_pos;
	
	TetraPartitionCL partition;
	QuadDomainCL q5dom;
	InterfaceTetraCL cut;
	const PrincipalLatticeCL lat(PrincipalLatticeCL::instance(2));
    std::valarray<double> ls_loc(lat.vertex_size());
	
	
	double SumErr=0.;
	double volume=0.;
	double average=0.;
    for (MultiGridCL::const_TriangTetraIteratorCL sit= const_cast<const MultiGridCL&>(MG_).GetTriangTetraBegin(lvl),
        send= const_cast<const MultiGridCL&>(MG_).GetTriangTetraEnd(lvl); sit != send; ++sit)
    {
		 evaluate_on_vertexes( lset.GetSolution(), *sit, lat, Addr( ls_loc));
		 const bool noCut= equal_signs( ls_loc);	 
         GetTrafoTr(T,det,*sit);
         const double absdet= std::fabs(det);
         LocalP1CL<double> loc_pr(*sit, make_P1Eval(MG_,BndData_.Pr,*DescPr));
         LocalP1CL<double> loc_Refpr(*sit, make_P1Eval(MG_,BndData_.Pr,*RefPr));		 
		 volume += sit->GetVolume();
		 if(noCut){
            q5_pr_exact.assign(loc_Refpr);
            q5_pr.assign(loc_pr);
			 SumErr += Quad5CL<> (q5_pr-q5_pr_exact).quad(absdet);			 
		 }
		 else{
			 partition.make_partition<SortedVertexPolicyCL, MergeCutPolicyCL>( lat, ls_loc);
			 make_CompositeQuad2Domain( q5dom, partition);
			 LocalP1CL<double> loc_pr_neg(*sit, make_P1Eval(MG_,BndData_.Pr, DescPr_neg));
			 LocalP1CL<double> loc_pr_pos(*sit, make_P1Eval(MG_,BndData_.Pr, DescPr_pos));
			 LocalP1CL<double> loc_Refpr_neg(*sit, make_P1Eval(MG_,BndData_.Pr, *RefPr));
			 LocalP1CL<double> loc_Refpr_pos(*sit, make_P1Eval(MG_,BndData_.Pr, *RefPr));
			 resize_and_evaluate_on_vertexes( loc_pr_neg, q5dom, pre_neg);
			 resize_and_evaluate_on_vertexes( loc_pr_pos, q5dom, pre_pos); 
			 
			 resize_and_evaluate_on_vertexes( loc_Refpr_neg, q5dom, pre_exact1);
			 resize_and_evaluate_on_vertexes( loc_Refpr_pos, q5dom, pre_exact2); 	
			 
			 SumErr+= quad ( (pre_neg-pre_exact1), absdet, q5dom, NegTetraC);
			 SumErr+= quad ( (pre_pos-pre_exact2), absdet, q5dom, PosTetraC);	
		 }
     }
	 //Get the average pressure, use it to normalize the pressure;
	average= SumErr/volume;
	std::cout<<"The average of the pressure error is: "<<average<<" The volume is: "<<volume<<std::endl;
    for (MultiGridCL::const_TriangTetraIteratorCL sit= const_cast<const MultiGridCL&>(MG_).GetTriangTetraBegin(lvl),
        send= const_cast<const MultiGridCL&>(MG_).GetTriangTetraEnd(lvl); sit != send; ++sit)
    {
		 evaluate_on_vertexes( lset.GetSolution(), *sit, lat, Addr( ls_loc));
		 cut.Init( *sit, lset.Phi, lset.GetBndData());
		 const bool noCut= equal_signs( ls_loc);
		 	
         GetTrafoTr(T,det,*sit);
         const double absdet= std::fabs(det);
         LocalP2CL<Point3DCL> loc_vel(*sit, make_P2Eval(MG_,BndData_.Vel,*DescVel));
         LocalP2CL<Point3DCL> loc_Refvel(*sit, make_P2Eval(MG_,BndData_.Vel,*RefVel));
         LocalP1CL<double> loc_pr(*sit, make_P1Eval(MG_,BndData_.Pr,*DescPr));
         LocalP1CL<double> loc_Refpr(*sit, make_P1Eval(MG_,BndData_.Pr,*RefPr));
		  LocalP1CL<double> loc_aver(average);
			 
         q5_vel.assign(loc_vel);
         q5_vel_exact.assign(loc_Refvel);
         Quad5CL<Point3DCL> q5_vel_diff( q5_vel-q5_vel_exact);
         L2_vel += Quad5CL<> (dot(q5_vel_diff,q5_vel_diff)).quad(absdet);
		 
		 if(noCut){

            q5_pr_exact.assign(loc_Refpr);
			 LocalP1CL<double> temp3(loc_pr-loc_aver);
			 q5_pr.assign(temp3);
	
			 Quad5CL<double> q5_pr_diff( q5_pr-q5_pr_exact);

			 L2_pr  += Quad5CL<> (q5_pr_diff*q5_pr_diff).quad(absdet);			 
		 }
		 else{
			 partition.make_partition<SortedVertexPolicyCL, MergeCutPolicyCL>( lat, ls_loc);
			 make_CompositeQuad2Domain( q5dom, partition);
			 LocalP1CL<double> loc_pr_neg(*sit, make_P1Eval(MG_,BndData_.Pr, DescPr_neg));
			 LocalP1CL<double> loc_pr_pos(*sit, make_P1Eval(MG_,BndData_.Pr, DescPr_pos));
			 LocalP1CL<double> temp1(loc_pr_neg-loc_aver); //for negative pressure;
			 LocalP1CL<double> temp2(loc_pr_pos-loc_aver); //for positive pressure;
			 LocalP2CL<double> pr_neg_l2 (temp1);
			 LocalP2CL<double> pr_pos_l2 (temp2);
			 LocalP2CL<double> pr_ex_neg (*sit, make_P1Eval(MG_,BndData_.Pr, *RefPr));
			 LocalP2CL<double> pr_ex_pos (*sit, make_P1Eval(MG_,BndData_.Pr, *RefPr));
			 LocalP2CL<double> err_neg_l2( (pr_neg_l2-pr_ex_neg)*(pr_neg_l2-pr_ex_neg) );
			 LocalP2CL<double> err_pos_l2( (pr_pos_l2-pr_ex_pos)*(pr_pos_l2-pr_ex_pos) );
			 
			 resize_and_evaluate_on_vertexes(err_neg_l2,  q5dom,  err_neg);
			 resize_and_evaluate_on_vertexes(err_pos_l2,  q5dom,  err_pos); 
			 //To do: currently not find the best way, use the time to as level set sign flag;			 
			 L2_pr += quad ( err_pos,  absdet, q5dom,   PosTetraC);
			 L2_pr += quad ( err_neg,  absdet, q5dom,   NegTetraC);	 
		 }
     }
     L2_vel  = std::sqrt(L2_vel);               //L2_vel is the true value.
     L2_pr   = std::sqrt(L2_pr);
         std::cout << "---------------------Discretize error-------------------"
		            <<"\n || u_h - u ||_L2 = " <<  L2_vel 
				     <<"\n || p_h - p ||_L2 = " <<  L2_pr << std::endl;	
}

///> To do, parallel case
double InstatStokes2PhaseP2P1CL::GetKineticEnergy(const LevelsetP2CL& lset) const
{
	const DROPS::Uint lvl = vel_idx.TriangLevel();
	const PrincipalLatticeCL lat(PrincipalLatticeCL::instance(2));
    std::valarray<double> ls_loc(lat.vertex_size());
    Quad5CL<Point3DCL> q5_vel;
    Quad5CL<double> q5_vel_sq;
    SMatrixCL<3,3> T;
    double det;

	TetraPartitionCL partition;
	QuadDomainCL q5dom;

	GridFunctionCL<Point3DCL> qvel;
	GridFunctionCL<double> qvel_sq;
	const_DiscVelSolCL vel =  GetVelSolution(v);
	double energy(0.0);
	double pos,neg;
	double trash;
	double prho =Coeff_.rho(1.),nrho=Coeff_.rho(-1.);
	DROPS_FOR_TRIANG_TETRA( MG_, lvl, it){

		GetTrafoTr(T,det,*it);
		const double absdet= std::fabs(det);
		evaluate_on_vertexes( lset.GetSolution(), *it, lat, Addr( ls_loc));
		const bool noCut= equal_signs( ls_loc);

		LocalP2CL<Point3DCL> loc_vel(*it, vel);

		if(noCut)
		{
			q5_vel.assign(loc_vel);
			q5_vel_sq=dot(q5_vel,q5_vel);
			if(ls_loc[0]>0)
				energy += q5_vel_sq.quad(absdet)*prho;
			else
				energy += q5_vel_sq.quad(absdet)*nrho;
		}
		else{
			partition.make_partition<SortedVertexPolicyCL, MergeCutPolicyCL>( lat, ls_loc);
			make_CompositeQuad5Domain( q5dom, partition);

			resize_and_evaluate_on_vertexes( loc_vel, q5dom, qvel);
			qvel_sq = dot(qvel,qvel);
			quad (qvel_sq , absdet, q5dom, trash, pos);
			quad (qvel_sq , absdet, q5dom, neg, trash);
			energy += pos*prho + neg*nrho;
		}
	}
	return std::sqrt(energy);
}

/*void InstatStokes2PhaseP2P1CL::SetupVelError(const instat_vector_fun_ptr RefVel)
{
	
    VectorCL& lsgvel= dv.Data;
    Uint lvl= dv.GetLevel(),
         idx= dv.RowIdx->GetIdx();
	Point3DCL tmp;
    for (MultiGridCL::const_TriangVertexIteratorCL sit= const_cast<const MultiGridCL&>(MG_).GetTriangVertexBegin(lvl), send= const_cast<const MultiGridCL&>(MG_).GetTriangVertexEnd(lvl);
         sit != send; ++sit)
    {
        if (sit->Unknowns.Exist(idx))
			DoFHelperCL<Point3DCL, VectorCL>::set( lsgvel, sit->Unknowns(idx), RefVel(sit->GetCoord(), 0));
    }

    for (MultiGridCL::const_TriangEdgeIteratorCL sit=const_cast<const MultiGridCL&>(MG_).GetTriangEdgeBegin(lvl), send=const_cast<const MultiGridCL&>(MG_).GetTriangEdgeEnd(lvl);
         sit != send; ++sit)
    {
        if (sit->Unknowns.Exist(idx))
        {
            tmp= RefVel( (sit->GetVertex(0)->GetCoord() + sit->GetVertex(1)->GetCoord())/2., 0);
            for(int i=0; i<3; ++i)
                lsgvel[sit->Unknowns(idx)+i]= tmp[i];
        }
    }	
	dv.Data-=v.Data;
}*/

P1XRepairCL::P1XRepairCL (MultiGridCL& mg, VecDescCL& p)
    : UsesXFEM_( p.RowIdx->IsExtended()), mg_( mg), idx_( P1_FE), ext_( &idx_), p_( p)
{
    if (!UsesXFEM_) return; // Do nothing, if we do not use P1X-elements.

    const ExtIdxDescCL& extidx= p_.RowIdx->GetXidx();
//     size_t extbegin( extidx.GetNumUnknownsStdFE());
    size_t extunknown;
    Uint repairidx( idx_.GetIdx()),
         pidx( p.RowIdx->GetIdx());

    idx_.CreateNumbering( p.RowIdx->TriangLevel(), mg);
    // Save the extended unknown-values.
    ext_.SetIdx( &idx_);

    // Attach the extended index to the vertex, so that it survives grid modifications.
    // We assume that all vertices have p-unknowns (like e. g. the pressure).
    DROPS_FOR_TRIANG_VERTEX( mg, p.RowIdx->TriangLevel(), it) {
        if (!it->Unknowns.Exist( pidx)) continue;
        if ( (extunknown= extidx[it->Unknowns( pidx)]) != NoIdx ) {
            ext_.Data[ it->Unknowns( repairidx)]= p.Data[extunknown];
        }
        else{
            it->Unknowns( repairidx)= NoIdx;
        }
    }
}

void P1XRepairCL::operator() ()
{
    if (!UsesXFEM_) return; // Do nothing, if we do not use P1X-elements.

    // We assume that the caller has repaired p as a P1-FE-variable.
    // Thus we only repair the extended part of p.
    size_t ci= 0;
    size_t extunknown;
    Uint repairidx( idx_.GetIdx()),
         pidx( p_.RowIdx->GetIdx());
    const ExtIdxDescCL& extidx= p_.RowIdx->GetXidx();
    // We assume that all vertices in p's level hold a p1-value. Thus, it->Unknowns.Exist()
    // can be spared.
    DROPS_FOR_TRIANG_VERTEX( mg_, p_.RowIdx->TriangLevel(), it) {
        if (!it->Unknowns.Exist( pidx)) continue;
        if ( ((extunknown= extidx[it->Unknowns( pidx)]) != NoIdx) && it->Unknowns.Exist( repairidx) ) {
            p_.Data[extunknown]= ext_.Data[it->Unknowns( repairidx)];
            ++ci;
        }
    }
//     std::cout << "P1XRepairCL::(): #P1-unknowns: " << extidx.GetNumUnknownsStdFE()
//               << "\t#copied extended-dof: " << ci << '\n';
}


void SetupMassDiag_P1(const MultiGridCL& MG, VectorCL& M, const IdxDescCL& RowIdx, const BndCondCL& bnd)
{
    ScopeTimerCL scope("SetupMassDiag_P1");

    M.resize( RowIdx.NumUnknowns());

    const Uint lvl= RowIdx.TriangLevel();
    LocalNumbP1CL Numb;

    DROPS_FOR_TRIANG_CONST_TETRA( MG, lvl, sit) {
        const double absdet= sit->GetVolume()*6.;
        Numb.assign( *sit, RowIdx, bnd);
        for(int i=0; i<4; ++i)
            if (Numb.WithUnknowns( i))
                M[Numb.num[i]]+= P1DiscCL::GetMass( i, i)*absdet;
    }
}

void SetupMassDiag_P1X (const MultiGridCL& MG, VectorCL& M, const IdxDescCL& RowIdx, const VecDescCL& lset,
                        const BndDataCL<>& lsetbnd, const BndCondCL& bnd)
{
    ScopeTimerCL scope("SetupMassDiag_P1X");

    const ExtIdxDescCL& Xidx= RowIdx.GetXidx();
    M.resize( RowIdx.NumUnknowns());

    const Uint lvl= RowIdx.TriangLevel();
    LocalNumbP1CL Numb;
    double coup[4], coupT2[4];

    double integralp;
    InterfaceTetraCL cut;
    bool sign[4];

    // The 4 squares of the P1-shape-functions
    LocalP2CL<> pi2[4];
    for(int i= 0; i < 4; ++i) {
        pi2[i][i]= 1.;
        for (int vert= 0; vert < 3; ++vert)
            pi2[i][EdgeByVert( i, VertOfFace( i, vert)) + 4]= 0.25;
    }

    LocalP2CL<> loc_phi;
    DROPS_FOR_TRIANG_CONST_TETRA( MG, lvl, sit) {
        const double absdet= sit->GetVolume()*6.;
        loc_phi.assign( *sit, lset, lsetbnd);
        cut.Init( *sit, loc_phi);
        const bool nocut= !cut.Intersects();
        Numb.assign( *sit, RowIdx, bnd);
        if (nocut) {
            for(int i= 0; i < 4; ++i)
                if ( Numb.WithUnknowns( i))
                    M[Numb.num[i]]+= P1DiscCL::GetMass( i, i)*absdet;
        }
        else { // extended basis functions have only support on tetra intersecting Gamma!
            for(int i=0; i<4; ++i) {
                sign[i]= cut.GetSign(i) == 1;
                // compute the integrals
                // \int_{T_2} p_i^2 dx,    where T_2 = T \cap \Omega_2
                integralp= 0.;
                for (int ch= 0; ch < 8; ++ch) {
                    cut.ComputeCutForChild( ch);
                    integralp+= cut.quad( pi2[i], absdet, true);  // integrate on positive part
                }
                coup[i]= P1DiscCL::GetMass( i, i)*absdet;
                coupT2[i]= integralp;
            }

            // write values into matrix
            for(int i=0; i<4; ++i) {
                if (!Numb.WithUnknowns( i)) continue;
                M[Numb.num[i]]+= coup[i];
                const IdxT xidx_i= Xidx[Numb.num[i]];
                if (xidx_i!=NoIdx)
                    M[xidx_i]+= coupT2[i]*(1 - 2*sign[i]) + sign[i]*coup[i];
            }
        }
    }
}

void SetupMassDiag_vecP2(const MultiGridCL& MG, VectorCL& M, const IdxDescCL& RowIdx, const BndCondCL& bnd)
{
    ScopeTimerCL scope("SetupMassDiag_vecP2");

    M.resize( RowIdx.NumUnknowns());

    const Uint lvl= RowIdx.TriangLevel();
    LocalNumbP2CL Numb;

    DROPS_FOR_TRIANG_CONST_TETRA( MG, lvl, sit) {
        const double absdet= sit->GetVolume()*6.;
        Numb.assign( *sit, RowIdx, bnd);
        for(int i=0; i<10; ++i)
            if (Numb.WithUnknowns( i)) {
                const double contrib= P2DiscCL::GetMass( i, i)*absdet;
                M[Numb.num[i]  ]+= contrib;
                M[Numb.num[i]+1]+= contrib;
                M[Numb.num[i]+2]+= contrib;
            }
    }
}

void SetupMassDiag (const MultiGridCL& MG, VectorCL& M, const IdxDescCL& RowIdx, const BndCondCL& bnd, const VecDescCL* lsetp, const BndDataCL<>* lsetbnd)
{
    switch(RowIdx.GetFE())
    {
    case P1_FE:
        SetupMassDiag_P1( MG, M, RowIdx, bnd); break;
    case P1X_FE:
        SetupMassDiag_P1X( MG, M, RowIdx, *lsetp, *lsetbnd, bnd); break;
    case vecP2_FE:
        SetupMassDiag_vecP2( MG, M, RowIdx, bnd); break;
    default:
        throw DROPSErrCL("SetupMassDiag not implemented for this FE type");
    }
}



void SetupLumpedMass_P1(const MultiGridCL& MG, VectorCL& M, const IdxDescCL& RowIdx, const BndCondCL& bnd)
{
    ScopeTimerCL scope("SetupLumpedMass_P1");
    M.resize( RowIdx.NumUnknowns());

    const Uint lvl= RowIdx.TriangLevel();
    LocalNumbP1CL Numb;

    DROPS_FOR_TRIANG_CONST_TETRA( MG, lvl, sit) {
        const double absdet= sit->GetVolume()*6.;
        Numb.assign( *sit, RowIdx, bnd);
        for(int i=0; i<4; ++i)
            if (Numb.WithUnknowns( i))
                M[Numb.num[i]]+= P1DiscCL::GetLumpedMass( i)*absdet;
    }
}

void SetupLumpedMass_P1X (const MultiGridCL& MG, VectorCL& M, const IdxDescCL& RowIdx, const VecDescCL& lset, const BndDataCL<>& lsetbnd, const BndCondCL& bnd)
{
    ScopeTimerCL scope("SetupLumpedMass_P1X");

    const ExtIdxDescCL& Xidx= RowIdx.GetXidx();
    M.resize( RowIdx.NumUnknowns());

    const Uint lvl= RowIdx.TriangLevel();
    LocalNumbP1CL Numb;
    double coup[4], coupT2[4];

    double integralp;
    InterfaceTetraCL cut;
    bool sign[4];

    // The 4 P1-shape-functions
    LocalP2CL<> pi[4];
    for(int i= 0; i < 4; ++i) {
        pi[i][i]= 1.;
        for (int vert= 0; vert < 3; ++vert)
            pi[i][EdgeByVert( i, VertOfFace( i, vert)) + 4]= 0.5;
    }

    LocalP2CL<> loc_phi;
    DROPS_FOR_TRIANG_CONST_TETRA( MG, lvl, sit) {
        const double absdet= sit->GetVolume()*6.;
        loc_phi.assign( *sit, lset, lsetbnd);
        cut.Init( *sit, loc_phi);
        const bool nocut= !cut.Intersects();
        Numb.assign( *sit, RowIdx, bnd);
        if (nocut) {
            for(int i= 0; i < 4; ++i)
                if ( Numb.WithUnknowns( i))
                    M[Numb.num[i]]+= P1DiscCL::GetMass( i, i)*absdet;
        }
        else { // extended basis functions have only support on tetra intersecting Gamma!
            for(int i=0; i<4; ++i) {
                sign[i]= cut.GetSign(i) == 1;
                // compute the integrals
                // \int_{T_2} p_i dx,    where T_2 = T \cap \Omega_2
                integralp= 0.;
                for (int ch= 0; ch < 8; ++ch) {
                    cut.ComputeCutForChild( ch);
                    integralp+= cut.quad( pi[i], absdet, true);  // integrate on positive part
                }
                coup[i]= P1DiscCL::GetLumpedMass( i)*absdet;
                coupT2[i]= integralp;
            }

            // write values into matrix
            for(int i=0; i<4; ++i) {
                if (!Numb.WithUnknowns( i)) continue;
                M[Numb.num[i]]+= coup[i];
                const IdxT xidx_i= Xidx[Numb.num[i]];
                if (xidx_i!=NoIdx)
                    M[xidx_i]+= coupT2[i]*(1 - 2*sign[i]) + sign[i]*coup[i];
            }
        }
    }
}

void SetupLumpedMass_vecP2(const MultiGridCL& MG, VectorCL& M, const IdxDescCL& RowIdx, const BndCondCL& bnd)
{
    ScopeTimerCL scope("SetupLumpedMass_vecP2");

    M.resize( RowIdx.NumUnknowns());

    const Uint lvl= RowIdx.TriangLevel();
    LocalNumbP2CL Numb;

    DROPS_FOR_TRIANG_CONST_TETRA( MG, lvl, sit) {
        const double absdet= sit->GetVolume()*6.;
        Numb.assign( *sit, RowIdx, bnd);
        for(int i=0; i<10; ++i)
            if (Numb.WithUnknowns( i)) {
                const double contrib= P2DiscCL::GetLumpedMass( i)*absdet;
                M[Numb.num[i]  ]+= contrib;
                M[Numb.num[i]+1]+= contrib;
                M[Numb.num[i]+2]+= contrib;
            }
    }
}

void SetupLumpedMass_vecP2R (const MultiGridCL& MG, VectorCL& M, const IdxDescCL& RowIdx, const VecDescCL& lset, const BndDataCL<>& lsetbnd, const BndCondCL& bnd)
{
    ScopeTimerCL scope("SetupLumpedMass_vecP2R");

    const ExtIdxDescCL& Xidx= RowIdx.GetXidx();
    M.resize( RowIdx.NumUnknowns());

    const Uint lvl= RowIdx.TriangLevel();
    LocalNumbP2CL Numb;
    double contribExt[4];

    InterfaceTetraCL cut;
    LocalP2CL<> p1abs_p[4][8], p1abs_n[4][8]; // extended basis functions on pos./neg. part, resp., for each of the 8 regular children
    LocalP2CL<> loc_phi;
    DROPS_FOR_TRIANG_CONST_TETRA( MG, lvl, sit) {
        const double absdet= sit->GetVolume()*6.;
        loc_phi.assign( *sit, lset, lsetbnd);
        cut.Init( *sit, loc_phi);
        Numb.assign( *sit, RowIdx, bnd);
        // write standard FE values into matrix
        for(int i=0; i<10; ++i)
            if (Numb.WithUnknowns( i)) {
                const double contrib= P2DiscCL::GetLumpedMass( i)*absdet;
                M[Numb.num[i]  ]+= contrib;
                M[Numb.num[i]+1]+= contrib;
                M[Numb.num[i]+2]+= contrib;
            }

        if (cut.Intersects()) { // extended basis functions have only support on tetra intersecting Gamma!
            P2RidgeDiscCL::GetExtBasisOnChildren(p1abs_p, p1abs_n, loc_phi);
            for(int i=0; i<4; ++i)
                contribExt[i]= 0;
            // compute integrals    int_T v_i^R dx
            for (int ch= 0; ch < 8; ++ch) {
                cut.ComputeCutForChild( ch);
                for(int i=0; i<4; ++i) {
                    contribExt[i]+= cut.quad( p1abs_p[i][ch], absdet, true);   // integrate on positive part
                    contribExt[i]+= cut.quad( p1abs_n[i][ch], absdet, false);  // integrate on negative part
                }
            }

            // write extended values into matrix
            for(int i=0; i<4; ++i) {
                if (!Numb.WithUnknowns( i)) continue;
                const IdxT xidx_i= Xidx[Numb.num[i]];
                if (xidx_i!=NoIdx) {
                    M[xidx_i  ]+= contribExt[i];
                    M[xidx_i+1]+= contribExt[i];
                    M[xidx_i+2]+= contribExt[i];
                }
            }
        }
    }
}

void SetupLumpedMass (const MultiGridCL& MG, VectorCL& M, const IdxDescCL& RowIdx, const BndCondCL& bnd, const VecDescCL* lsetp, const BndDataCL<>* lsetbnd)
{
    switch(RowIdx.GetFE())
    {
    case P1_FE:
        SetupLumpedMass_P1( MG, M, RowIdx, bnd); break;
    case P1X_FE:
        SetupLumpedMass_P1X( MG, M, RowIdx, *lsetp, *lsetbnd, bnd); break;
    case vecP2_FE:
        SetupLumpedMass_vecP2( MG, M, RowIdx, bnd); break;
    case vecP2R_FE:
        SetupLumpedMass_vecP2R( MG, M, RowIdx, *lsetp, *lsetbnd, bnd); break;
    default:
        throw DROPSErrCL("SetupLumpedMass not implemented for this FE type");
    }
}

} // end of namespace DROPS


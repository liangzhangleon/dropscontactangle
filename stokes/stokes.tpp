//**************************************************************************
// File:    stokes.tpp                                                     *
// Content: classes that constitute the stokes-problem                     *
// Author:  Sven Gross, Joerg Peters, Volker Reichelt, IGPM RWTH Aachen    *
// Version: 0.1                                                            *
// History: begin - March, 16 2001                                         *
//**************************************************************************

#ifndef _STOKES_TPP_
#define _STOKES_TPP_

#include "num/discretize.h"
#include "misc/problem.h"
#include <vector>
#include <numeric>

namespace DROPS
{

template <class MGB, class Coeff>
void StokesP2P1CL<MGB,Coeff>::GetDiscError(vector_fun_ptr LsgVel, scalar_fun_ptr LsgPr) const
{
    Uint lvl= A.RowIdx->TriangLevel,
        vidx= A.RowIdx->Idx,
        pidx= B.RowIdx->Idx;
    VectorCL lsgvel(A.RowIdx->NumUnknowns);
    VectorCL lsgpr( B.RowIdx->NumUnknowns);

    for (MultiGridCL::const_TriangVertexIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangVertexBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangVertexEnd(lvl);
         sit != send; ++sit)
    {
        if (!_BndData.Vel.IsOnDirBnd(*sit))
        {
           for(int i=0; i<3; ++i)
               lsgvel[sit->Unknowns(vidx)[i]]= LsgVel(sit->GetCoord())[i];
        }
    }
    
    for (MultiGridCL::const_TriangEdgeIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangEdgeBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangEdgeEnd(lvl);
         sit != send; ++sit)
    {
        if (!_BndData.Vel.IsOnDirBnd(*sit))
        {
           for(int i=0; i<3; ++i)
                lsgvel[sit->Unknowns(vidx)[i]]= LsgVel( (sit->GetVertex(0)->GetCoord() + sit->GetVertex(1)->GetCoord())/2.)[i];
        }
    }
    for (MultiGridCL::const_TriangVertexIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangVertexBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangVertexEnd(lvl);
         sit != send; ++sit)
        lsgpr[sit->Unknowns(pidx)[0]]= LsgPr(sit->GetCoord());

    std::cerr << "discretization error to check the system (x,y = continuos solution): "<<std::endl;
    VectorCL res= A.Data*lsgvel + transp_mul(B.Data,lsgpr)-b.Data; 
    std::cerr <<"|| Ax + BTy - f || = "<< res.norm()<<", max "<<res.supnorm()<<std::endl;
    VectorCL resB= B.Data*lsgvel - c.Data; 
    std::cerr <<"|| Bx - g || = "<< resB.norm()<<", max "<<resB.supnorm()<<std::endl;
}



/*****************************************************************************************************
* formulas for   n u m e r i c   i n t e g r a t i o n   on the reference tetrahedron
*****************************************************************************************************/

inline StokesVelBndDataCL::bnd_type Quad(const TetraCL& t, vector_fun_ptr coeff)
// exact up to degree 2
{
    return ( coeff(t.GetVertex(0)->GetCoord())
            +coeff(t.GetVertex(1)->GetCoord())
            +coeff(t.GetVertex(2)->GetCoord())
            +coeff(t.GetVertex(3)->GetCoord()))/120. 
            + 2./15.*coeff(GetBaryCenter(t));
}

inline double QuadGrad(const SMatrixCL<3,5>* G, int i, int j)
// computes int( grad(phi_i) * grad(phi_j) ) for P2-elements on ref. tetra
{
    SVectorCL<5> tmp(0.0);
    
    for(int k=0; k<5; ++k)
        for(int l=0; l<3; ++l)
            tmp[k]+= G[i](l,k) * G[j](l,k);
            
    return ( tmp[0] + tmp[1] + tmp[2] + tmp[3] )/120. + 2./15.*tmp[4];
}

inline SVectorCL<3> Quad( const TetraCL& t, vector_fun_ptr coeff, int i)
{
    SVectorCL<3> f[5];
    
    if (i<4) // hat function on vert
    {
        f[0]= coeff( t.GetVertex(i)->GetCoord() );
        for (int k=0, l=1; k<4; ++k)
            if (k!=i) f[l++]= coeff( t.GetVertex(k)->GetCoord() );
        f[4]= coeff( GetBaryCenter(t) );
        return f[0]/504. - (f[1] + f[2] + f[3])/1260. - f[4]/126.;
    }
    else  // hat function on edge
    {
        const double ve= 4./945.,  // coeff for verts of edge
                     vn= -1./756.,  // coeff for other verts
                     vs= 26./945.;   // coeff for barycenter
        double a[4];
        a[VertOfEdge(i-4,0)]= a[VertOfEdge(i-4,1)]= ve;
        a[VertOfEdge(OppEdge(i-4),0)]= a[VertOfEdge(OppEdge(i-4),1)]= vn;

        SVectorCL<3> sum= vs * coeff( GetBaryCenter(t) );
        for(int k=0; k<4; ++k)
            sum+= a[k] * coeff( t.GetVertex(k)->GetCoord() );

        return sum;
    }
}


inline SVectorCL<3> Quad(SVectorCL<3> f[5], int i)
{
    if (i<4) // hat function on vert
    {
        std::swap(f[0], f[i]);
        return f[0]/504. - (f[1] + f[2] + f[3])/1260. - f[4]/126.;
    }
    else  // hat function on edge
    {
        const double ve= 4./945.,  // coeff for verts of edge
                     vn= -1./756.,  // coeff for other verts
                     vs= 26./945.;   // coeff for barycenter
        double a[4];
        a[VertOfEdge(i-4,0)]= a[VertOfEdge(i-4,1)]= ve;
        a[VertOfEdge(OppEdge(i-4),0)]= a[VertOfEdge(OppEdge(i-4),1)]= vn;

        SVectorCL<3> sum= vs * f[4];
        for(int k=0; k<4; ++k)
            sum+= a[k] * f[k];

        return sum;
    }
}


inline double Quad( const TetraCL& t, scalar_fun_ptr f, int i, int j)
{
    double a[5];
    if (i>j) std::swap(i,j);
    switch(i*10+j)
    {
      case  0: a[0]= 1./1260.; a[1]= a[2]= a[3]= 0.; a[4]= 1./630.; break;
      case  1: a[0]= a[1]= -1./8505.; a[2]= a[3]= 11./136080.; a[4]= 4./8505; break;
      case  2: a[0]= a[2]= -1./8505.; a[1]= a[3]= 11./136080.; a[4]= 4./8505; break;
      case  3: a[0]= a[3]= -1./8505.; a[1]= a[2]= 11./136080.; a[4]= 4./8505; break;
      case  4: a[0]= 1./2520.; a[1]= -1./2520.; a[2]= a[3]= 0.; a[4]= -1./630.; break;
      case  5: a[0]= 1./2520.; a[2]= -1./2520.; a[1]= a[3]= 0.; a[4]= -1./630.; break;
      case  6: a[0]= a[3]= 1./8505.; a[1]= a[2]= -19./68040.; a[4]= -1./486.; break;
      case  7: a[0]= 1./2520.; a[3]= -1./2520.; a[1]= a[2]= 0.; a[4]= -1./630.; break;
      case  8: a[0]= a[2]= 1./8505.; a[1]= a[3]= -19./68040.; a[4]= -1./486.; break;
      case  9: a[0]= a[1]= 1./8505.; a[2]= a[3]= -19./68040.; a[4]= -1./486.; break;
      case 11: a[1]= 1./1260.; a[0]= a[2]= a[3]= 0.; a[4]= 1./630.; break;
      case 12: a[1]= a[2]= -1./8505.; a[0]= a[3]= 11./136080.; a[4]= 4./8505; break;
      case 13: a[1]= a[3]= -1./8505.; a[0]= a[2]= 11./136080.; a[4]= 4./8505; break;
      case 14: a[1]= 1./2520.; a[0]= -1./2520.; a[2]= a[3]= 0.; a[4]= -1./630.; break;
      case 15: a[1]= a[3]= 1./8505.; a[0]= a[2]= -19./68040.; a[4]= -1./486.; break;
      case 16: a[1]= 1./2520.; a[2]= -1./2520.; a[0]= a[3]= 0.; a[4]= -1./630.; break;
      case 17: a[1]= a[2]= 1./8505.; a[0]= a[3]= -19./68040.; a[4]= -1./486.; break;
      case 18: a[1]= 1./2520.; a[3]= -1./2520.; a[0]= a[2]= 0.; a[4]= -1./630.; break;
      case 19: a[0]= a[1]= 1./8505.; a[2]= a[3]= -19./68040.; a[4]= -1./486.; break;
      case 22: a[2]= 1./1260.; a[0]= a[1]= a[3]= 0.; a[4]= 1./630.; break;
      case 23: a[2]= a[3]= -1./8505.; a[0]= a[1]= 11./136080.; a[4]= 4./8505; break;
      case 24: a[2]= a[3]= 1./8505.; a[0]= a[1]= -19./68040.; a[4]= -1./486.; break;
      case 25: a[2]= 1./2520.; a[0]= -1./2520.; a[1]= a[3]= 0.; a[4]= -1./630.; break;
      case 26: a[2]= 1./2520.; a[1]= -1./2520.; a[0]= a[3]= 0.; a[4]= -1./630.; break;
      case 27: a[2]= a[1]= 1./8505.; a[0]= a[3]= -19./68040.; a[4]= -1./486.; break;
      case 28: a[2]= a[0]= 1./8505.; a[1]= a[3]= -19./68040.; a[4]= -1./486.; break;
      case 29: a[2]= 1./2520.; a[3]= -1./2520.; a[0]= a[1]= 0.; a[4]= -1./630.; break;
      case 33: a[3]= 1./1260.; a[0]= a[1]= a[2]= 0.; a[4]= 1./630.; break;
      case 34: a[3]= a[2]= 1./8505.; a[0]= a[1]= -19./68040.; a[4]= -1./486.; break;
      case 35: a[3]= a[1]= 1./8505.; a[0]= a[2]= -19./68040.; a[4]= -1./486.; break;
      case 36: a[3]= a[0]= 1./8505.; a[1]= a[2]= -19./68040.; a[4]= -1./486.; break;
      case 37: a[3]= 1./2520.; a[0]= -1./2520.; a[1]= a[2]= 0.; a[4]= -1./630.; break;
      case 38: a[3]= 1./2520.; a[1]= -1./2520.; a[0]= a[2]= 0.; a[4]= -1./630.; break;
      case 39: a[3]= 1./2520.; a[2]= -1./2520.; a[0]= a[1]= 0.; a[4]= -1./630.; break;
      case 44: a[0]= a[1]= 37./17010.; a[2]= a[3]= -17./17010.; a[4]= 88./8505.; break;
      case 45: a[0]= 1./972.; a[1]= a[2]= 2./8505.; a[3]= -19./34020.; a[4]= 46./8505.; break;
      case 46: a[1]= 1./972.; a[0]= a[2]= 2./8505.; a[3]= -19./34020.; a[4]= 46./8505.; break;
      case 47: a[0]= 1./972.; a[1]= a[3]= 2./8505.; a[2]= -19./34020.; a[4]= 46./8505.; break;
      case 48: a[1]= 1./972.; a[0]= a[3]= 2./8505.; a[2]= -19./34020.; a[4]= 46./8505.; break;
      case 49: a[0]= a[1]= a[2]= a[3]= 1./11340.; a[4]= 8./2835.; break;
      case 55: a[0]= a[2]= 37./17010.; a[1]= a[3]= -17./17010.; a[4]= 88./8505.; break;
      case 56: a[2]= 1./972.; a[0]= a[1]= 2./8505.; a[3]= -19./34020.; a[4]= 46./8505.; break;
      case 57: a[0]= 1./972.; a[2]= a[3]= 2./8505.; a[1]= -19./34020.; a[4]= 46./8505.; break;
      case 58: a[0]= a[1]= a[2]= a[3]= 1./11340.; a[4]= 8./2835.; break;
      case 59: a[2]= 1./972.; a[0]= a[3]= 2./8505.; a[1]= -19./34020.; a[4]= 46./8505.; break;
      case 66: a[1]= a[2]= 37./17010.; a[0]= a[3]= -17./17010.; a[4]= 88./8505.; break;
      case 67: a[0]= a[1]= a[2]= a[3]= 1./11340.; a[4]= 8./2835.; break;
      case 68: a[1]= 1./972.; a[2]= a[3]= 2./8505.; a[0]= -19./34020.; a[4]= 46./8505.; break;
      case 69: a[2]= 1./972.; a[1]= a[3]= 2./8505.; a[0]= -19./34020.; a[4]= 46./8505.; break;
      case 77: a[0]= a[3]= 37./17010.; a[1]= a[2]= -17./17010.; a[4]= 88./8505.; break;
      case 78: a[3]= 1./972.; a[0]= a[1]= 2./8505.; a[2]= -19./34020.; a[4]= 46./8505.; break;
      case 79: a[3]= 1./972.; a[0]= a[2]= 2./8505.; a[1]= -19./34020.; a[4]= 46./8505.; break;
      case 88: a[1]= a[3]= 37./17010.; a[0]= a[2]= -17./17010.; a[4]= 88./8505.; break;
      case 89: a[3]= 1./972.; a[1]= a[2]= 2./8505.; a[0]= -19./34020.; a[4]= 46./8505.; break;
      case 99: a[2]= a[3]= 37./17010.; a[0]= a[1]= -17./17010.; a[4]= 88./8505.; break;
      default: throw DROPSErrCL("Quad(i,j): no such shape function");
    }
    double sum= a[4]*f(GetBaryCenter(t));
    for(Uint i=0; i<4; ++i)
        sum+= a[i]*f(t.GetVertex(i)->GetCoord());
    return sum;
}


inline StokesVelBndDataCL::bnd_type Quad2D(const TetraCL& t, Uint face, Uint vert, 
                                           StokesVelBndDataCL::bnd_val_fun bfun)
// Integrate bnd_val * phi_vert over face
{
    const VertexCL* v[3];
    
    v[0]= t.GetVertex(vert);
    for (int i=0, k=1; i<3; ++i)
    {
        if (VertOfFace(face,i)!=vert)
            v[k++]= t.GetVertex( VertOfFace(face,i) );
    }
    const StokesVelBndDataCL::bnd_type f0= bfun(v[0]->GetCoord());
    const StokesVelBndDataCL::bnd_type f1= bfun(v[1]->GetCoord()) +  bfun( v[2]->GetCoord());
    const StokesVelBndDataCL::bnd_type f2= bfun((v[0]->GetCoord() + v[1]->GetCoord() + v[2]->GetCoord())/3.0 );    //Barycenter of Face
    const double            absdet= FuncDet2D(v[1]->GetCoord() - v[0]->GetCoord(), v[2]->GetCoord() - v[0]->GetCoord());
    return (11./240.*f0 + 1./240.*f1 + 9./80.*f2) * absdet;
}


/**************************************************************************************************
* member functions to handle with index descriptions
**************************************************************************************************/

template <class MGB, class Coeff>
void StokesP2P1CL<MGB,Coeff>::CreateNumberingPr(Uint level, IdxDescCL* idx)
// used for numbering of the Unknowns depending on the index IdxDesc[idxnum].
// sets up the description of the index idxnum in IdxDesc[idxnum],
// allocates memory for the Unknown-Indices on TriangLevel level und numbers them.
// Remark: expects, thatIdxDesc[idxnr].NumUnknownsVertex etc. are set.
{
    // set up the index description
    idx->TriangLevel = level;
    idx->NumUnknowns = 0;

    const Uint idxnum = idx->Idx;    // idx is the index in UnknownIdxCL

    // allocate space for indices; number unknowns in TriangLevel level; "true" generates unknowns on
    // the boundary
    CreateNumbOnVertex( idxnum, idx->NumUnknowns, idx->NumUnknownsVertex,
                        _MG.GetTriangVertexBegin(level), _MG.GetTriangVertexEnd(level),
                        _BndData.Pr );
}

template <class MGB, class Coeff>
void StokesP2P1CL<MGB,Coeff>::CreateNumberingVel(Uint level, IdxDescCL* idx)
// used for numbering of the Unknowns depending on the index IdxDesc[idxnum].
// sets up the description of the index idxnum in IdxDesc[idxnum],
// allocates memory for the Unknown-Indices on TriangLevel level und numbers them.
// Remark: expects, thatIdxDesc[idxnr].NumUnknownsVertex etc. are set.
{
    // set up the index description
    idx->TriangLevel = level;
    idx->NumUnknowns = 0;

    const Uint idxnum = idx->Idx;    // idx is the index in UnknownIdxCL

    // allocate space for indices; number unknowns in TriangLevel level
    CreateNumbOnVertex( idxnum, idx->NumUnknowns, idx->NumUnknownsVertex,
                        _MG.GetTriangVertexBegin(level), _MG.GetTriangVertexEnd(level),
                        _BndData.Vel );
    CreateNumbOnEdge( idxnum, idx->NumUnknowns, idx->NumUnknownsEdge,
                      _MG.GetTriangEdgeBegin(level), _MG.GetTriangEdgeEnd(level),
                      _BndData.Vel );
}


template <class MGB, class Coeff>
void StokesP2P1CL<MGB,Coeff>::DeleteNumberingVel(IdxDescCL* idx)
{
    const Uint idxnum = idx->Idx;    // idx is the index in UnknownIdxCL
    const Uint level  = idx->TriangLevel;
    idx->NumUnknowns = 0;

    // delete memory allocated for indices
    DeleteNumbOnSimplex( idxnum, _MG.GetTriangVertexBegin(level), _MG.GetTriangVertexEnd(level) );
    DeleteNumbOnSimplex( idxnum, _MG.GetTriangEdgeBegin(level), _MG.GetTriangEdgeEnd(level) );
}

template <class MGB, class Coeff>
void StokesP2P1CL<MGB,Coeff>::DeleteNumberingPr(IdxDescCL* idx)
{
    const Uint idxnum = idx->Idx;    // idx is the index in UnknownIdxCL
    const Uint level  = idx->TriangLevel;
    idx->NumUnknowns = 0;

    // delete memory allocated for indices
    DeleteNumbOnSimplex( idxnum, _MG.GetTriangVertexBegin(level), _MG.GetTriangVertexEnd(level) );
}



inline void GetGradientsOnRef(SMatrixCL<3,5>* GRef)
{
    SVectorCL<3> vec;
    
    for(int i=0; i<10; ++i)
    {
        vec= FE_P2CL::DHRef(i,0,0,0);                // vertex 0
        GRef[i](0,0)= vec[0];   GRef[i](1,0)= vec[1];   GRef[i](2,0)= vec[2];
        vec= FE_P2CL::DHRef(i,1,0,0);                // vertex 1
        GRef[i](0,1)= vec[0];   GRef[i](1,1)= vec[1];   GRef[i](2,1)= vec[2];
        vec= FE_P2CL::DHRef(i,0,1,0);                // vertex 2
        GRef[i](0,2)= vec[0];   GRef[i](1,2)= vec[1];   GRef[i](2,2)= vec[2];
        vec= FE_P2CL::DHRef(i,0,0,1);                // vertex 3
        GRef[i](0,3)= vec[0];   GRef[i](1,3)= vec[1];   GRef[i](2,3)= vec[2];
        vec= FE_P2CL::DHRef(i,1./4.,1./4.,1./4.);    // barycenter
        GRef[i](0,4)= vec[0];   GRef[i](1,4)= vec[1];   GRef[i](2,4)= vec[2];
    }
}        


inline void MakeGradients (SMatrixCL<3,5>* G, const SMatrixCL<3,5>* GRef, const SMatrixCL<3,3>& T)
{
    for(int i=0; i<10; ++i)
        G[i]= T*GRef[i];
}

template <class MGB, class Coeff>
void StokesP2P1CL<MGB,Coeff>::SetupSystem(MatDescCL* matA, VelVecDescCL* vecA, MatDescCL* matB, VelVecDescCL* vecB) const
// Sets up the stiffness matrices and right hand sides
{
    vecA->Clear();
    vecB->Clear();
    
    const IdxT num_unks_vel= matA->RowIdx->NumUnknowns;
    const IdxT num_unks_pr=  matB->RowIdx->NumUnknowns;

    SparseMatBuilderCL<double> A(&matA->Data, num_unks_vel, num_unks_vel), 
                               B(&matB->Data, num_unks_pr,  num_unks_vel);
    VelVecDescCL& b   = *vecA;
    VelVecDescCL& c   = *vecB;
    const Uint lvl    = matA->RowIdx->TriangLevel;
    const Uint vidx   = matA->RowIdx->Idx,
               pidx   = matB->RowIdx->Idx;

    IdxT Numb[10], prNumb[4];
    bool IsOnDirBnd[10];
    
    const IdxT stride= 1;   // stride between unknowns on same simplex, which
                            // depends on numbering of the unknowns
                            
    std::cerr << "entering SetupSystem: " <<num_unks_vel<<" vels, "<<num_unks_pr<<" prs"<< std::endl;                            

    // fill value part of matrices
    SMatrixCL<3,5> Grad[10], GradRef[10];  // jeweils Werte des Gradienten in 5 Stuetzstellen
    SMatrixCL<3,3> T;
    double coup[10][10];
    double det, absdet;
    SVectorCL<3> tmp;

    GetGradientsOnRef(GradRef);
    
    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        GetTrafoTr(T,det,*sit);
        MakeGradients(Grad, GradRef, T);
        absdet= fabs(det);
        
        // collect some information about the edges and verts of the tetra
        // and save it in Numb and IsOnDirBnd
        for(int i=0; i<4; ++i)
        {
            if(!(IsOnDirBnd[i]= _BndData.Vel.IsOnDirBnd( *sit->GetVertex(i) )))
                Numb[i]= sit->GetVertex(i)->Unknowns(vidx)[0];
            prNumb[i]= sit->GetVertex(i)->Unknowns(pidx)[0];
        }
        for(int i=0; i<6; ++i)
        {
            if (!(IsOnDirBnd[i+4]= _BndData.Vel.IsOnDirBnd( *sit->GetEdge(i) )))
                Numb[i+4]= sit->GetEdge(i)->Unknowns(vidx)[0];
        }

        // compute all couplings between HatFunctions on edges and verts
        for(int i=0; i<10; ++i)
            for(int j=0; j<=i; ++j)
            {
                // negative dot-product of the gradients
                coup[i][j]= _Coeff.nu * QuadGrad( Grad, i, j)*absdet;
                coup[i][j]+= Quad(*sit, &_Coeff.q, i, j)*absdet;
                coup[j][i]= coup[i][j];
            }

        for(int i=0; i<10; ++i)    // assemble row Numb[i]
            if (!IsOnDirBnd[i])  // vert/edge i is not on a Dirichlet boundary
            {
                for(int j=0; j<10; ++j)
                {
                    if (!IsOnDirBnd[j]) // vert/edge j is not on a Dirichlet boundary
                    {
                        A(Numb[i],          Numb[j])+=          coup[j][i]; 
                        A(Numb[i]+stride,   Numb[j]+stride)+=   coup[j][i]; 
                        A(Numb[i]+2*stride, Numb[j]+2*stride)+= coup[j][i]; 
                    }
                    else // coupling with vert/edge j on right-hand-side
                    {
                        tmp= j<4 ? _BndData.Vel.GetDirBndValue(*sit->GetVertex(j))
                                 : _BndData.Vel.GetDirBndValue(*sit->GetEdge(j-4));
                        b.Data[Numb[i]]-=          coup[j][i] * tmp[0];
                        b.Data[Numb[i]+stride]-=   coup[j][i] * tmp[1];
                        b.Data[Numb[i]+2*stride]-= coup[j][i] * tmp[2];
                    }
                }
                tmp= Quad(*sit, &_Coeff.f, i)*absdet;
                b.Data[Numb[i]]+=          tmp[0];
                b.Data[Numb[i]+stride]+=   tmp[1];
                b.Data[Numb[i]+2*stride]+= tmp[2];

                if ( i<4 ? _BndData.Vel.IsOnNeuBnd(*sit->GetVertex(i))
                         : _BndData.Vel.IsOnNeuBnd(*sit->GetEdge(i-4)) ) // vert/edge i is on Neumann boundary
                {
                    Uint face;
                    for (int f=0; f < 3; ++f)
                    {
                        face= i<4 ? FaceOfVert(i,f) : FaceOfEdge(i-4,f);
                        if ( sit->IsBndSeg(face))
                        {   // TODO: FIXME: Hier muss doch eigentlich eine 2D-Integrationsformel fuer P2-Elemente stehen, oder?
                            tmp= Quad2D(*sit, face, i, _BndData.Vel.GetSegData(sit->GetBndIdx(face)).GetBndFun() );
                            b.Data[Numb[i]]+=          tmp[0];
                            b.Data[Numb[i]+stride]+=   tmp[1];
                            b.Data[Numb[i]+2*stride]+= tmp[2];
                        }
                    }
                }
            }
            
        // Setup B:   B(i,j) =  psi_i * div( phi_j)
        for(int vel=0; vel<10; ++vel)
        {
            if (!IsOnDirBnd[vel])
                for(int pr=0; pr<4; ++pr)
                {
                    // numeric integration is exact: psi_i * div( phi_j) is of degree 2 !
                    // hint: psi_i( barycenter ) = 0.25
                    B(prNumb[pr],Numb[vel])+=           (Grad[vel](0,pr) / 120. + Grad[vel](0,4)*0.25 * 2./15. )*absdet;
                    B(prNumb[pr],Numb[vel]+stride)+=    (Grad[vel](1,pr) / 120. + Grad[vel](1,4)*0.25 * 2./15. )*absdet;
                    B(prNumb[pr],Numb[vel]+2*stride)+=  (Grad[vel](2,pr) / 120. + Grad[vel](2,4)*0.25 * 2./15. )*absdet;
                }
            else // put coupling on rhs
            {
                tmp= vel<4 ? _BndData.Vel.GetDirBndValue( *sit->GetVertex(vel))
                           : _BndData.Vel.GetDirBndValue( *sit->GetEdge(vel-4));
                for(int pr=0; pr<4; ++pr)
                {
                    // numeric integration is exact: psi_i * div( phi_j) is of degree 2 !
                    c.Data[prNumb[pr]]-= (Grad[vel](0,pr) / 120. + Grad[vel](0,4)*0.25 * 2./15. )*absdet*tmp[0]
                                         +(Grad[vel](1,pr) / 120. + Grad[vel](1,4)*0.25 * 2./15. )*absdet*tmp[1]
                                         +(Grad[vel](2,pr) / 120. + Grad[vel](2,4)*0.25 * 2./15. )*absdet*tmp[2];
                }
            }
        }
    }
    std::cerr << "done: value part fill" << std::endl;

    A.Build();
    B.Build();
    std::cerr << matA->Data.num_nonzeros() << " nonzeros in A, "
              << matB->Data.num_nonzeros() << " nonzeros in B! " << std::endl;
}

template <class MGB, class Coeff>
void StokesP2P1CL<MGB,Coeff>::SetupMass(MatDescCL* matM) const
// Sets up the mass matrix
{
    const IdxT num_unks_pr=  matM->RowIdx->NumUnknowns;

    SparseMatBuilderCL<double> M(&matM->Data, num_unks_pr,  num_unks_pr);

    const Uint lvl    = matM->RowIdx->TriangLevel;
    const Uint pidx   = matM->RowIdx->Idx;

    IdxT prNumb[4];

    // compute all couplings between HatFunctions on verts:
    // I( i, j) = int ( psi_i*psi_j, T_ref) * absdet
    const double coupl_ii= 1./120. + 2./15./16.,
                 coupl_ij=           2./15./16.;

    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        const double absdet= sit->GetVolume()*6;
        
        for(int i=0; i<4; ++i)
            prNumb[i]= sit->GetVertex(i)->Unknowns(pidx)[0];

        for(int i=0; i<4; ++i)    // assemble row prNumb[i]
            for(int j=0; j<4; ++j)
                    M( prNumb[i], prNumb[j])+= (i==j ? coupl_ii : coupl_ij) * absdet;
    }
    M.Build();
}



template <class MGB, class Coeff>
void StokesP2P1CL<MGB,Coeff>::CheckSolution(const VelVecDescCL* lsgvel, const VecDescCL* lsgpr, 
                                 vector_fun_ptr LsgVel,      scalar_fun_ptr LsgPr) const
{
    double mindiff=0, maxdiff=0, norm2= 0;
    Uint lvl=lsgvel->RowIdx->TriangLevel;
    
    VectorCL res1= A.Data*lsgvel->Data + transp_mul( B.Data, lsgpr->Data ) - b.Data;
    VectorCL res2= B.Data*lsgvel->Data - c.Data;

    std::cerr << "\nChecken der Loesung...\n";    
    std::cerr << "|| Ax + BTy - F || = " << res1.norm() << ", max. " << res1.supnorm() << std::endl;
    std::cerr << "||       Bx - G || = " << res2.norm() << ", max. " << res2.supnorm() << std::endl<<std::endl;
    
    P1EvalCL<double, const StokesPrBndDataCL, const VecDescCL>  pr(lsgpr, &_BndData.Pr, &_MG);
    P2EvalCL<SVectorCL<3>, const StokesVelBndDataCL, const VelVecDescCL>  vel(lsgvel, &_BndData.Vel, &_MG);
    double L1_div= 0, L2_div= 0;
    SMatrixCL<3,3> T, M;
    double det, absdet;

    // Calculate div(u).
    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        double div[5]= {0., 0., 0., 0., 0.};   // Divergenz in den Verts und im BaryCenter
        GetTrafoTr(T,det,*sit);
        absdet= fabs(det);
        for(Uint i= 0; i<10; ++i)
        {
            const SVectorCL<3> value= i<4 ? vel.val(*sit->GetVertex(i))
                                    : vel.val(*sit->GetEdge(i-4));
            div[0]+= inner_prod(T*FE_P2CL::DHRef(i,0,0,0),value);
            div[1]+= inner_prod(T*FE_P2CL::DHRef(i,1,0,0),value);
            div[2]+= inner_prod(T*FE_P2CL::DHRef(i,0,1,0),value);
            div[3]+= inner_prod(T*FE_P2CL::DHRef(i,0,0,1),value);
            div[4]+= inner_prod(T*FE_P2CL::DHRef(i,0.25,0.25,0.25),value);
        }
        L1_div+= ( (fabs(div[0])+fabs(div[1])+fabs(div[2])+fabs(div[3]))/120 + fabs(div[4])*2./15. ) * absdet;
        L2_div+= ( (div[0]*div[0]+div[1]*div[1]+div[2]*div[2]+div[3]*div[3])/120 + div[4]*div[4]*2./15. ) * absdet;
    }
    L2_div= ::sqrt(L2_div);
    std::cerr << "|| div x ||_L1 = " << L1_div << std::endl;
    std::cerr << "|| div x ||_L2 = " << L2_div << std::endl << std::endl;

    // Compute the pressure-coefficient in direction of 1/sqrt(meas(Omega)), which eliminates
    // the allowed offset of the pressure by setting it to 0.
    double MW_pr= 0, vol= 0;
    const Uint numpts= Quad3CL::GetNumPoints();
    double* pvals= new double[numpts];
    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        const double volT= sit->GetVolume();
        for (Uint i=0; i<numpts; ++i)
        {
            const Point3DCL& p= Quad3CL::GetPoints()[i];
            pvals[i]= pr.val(*sit, p[0], p[1], p[2]);
        }
        MW_pr+= Quad3CL::Quad(pvals)*volT*6.;
        vol+= volT;
    }
    const double c_pr= MW_pr/vol;
    std::cerr << "\nconstant pressure offset is " << c_pr << ", volume of cube is " << vol << std::endl;

    // Some norms of velocities: u_h - u
    double L2_Dvel(0.0), L2_vel(0.0);
    double L2_pr(0.0);
    double* vals= new double[numpts];
    double* Dvals= new double[numpts];
    for(MultiGridCL::const_TriangTetraIteratorCL sit= const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send= const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
        sit != send; ++sit)
    {
        GetTrafoTr(M,det,*sit);
        const double absdet= fabs(det);
        // Put velocity degrees of freedom in veldof
        std::vector< SVectorCL<3> > veldof;
        veldof.reserve(10);
        vel.GetDoF(*sit, veldof);
        for (Uint i=0; i<numpts; ++i)
        {
            const Point3DCL& p= Quad3CL::GetPoints()[i];
            const Point3DCL  p_world= GetWorldCoord(*sit, p);

            const double prtmp= pr.val(*sit, p[0], p[1], p[2]) - c_pr - LsgPr(p_world);
            pvals[i]= prtmp*prtmp;
            const SVectorCL<3> tmp= vel.val(veldof, p[0], p[1], p[2]) - LsgVel(p_world);
            vals[i]= inner_prod(tmp, tmp);

            Dvals[i]= 0.;
            Point3DCL Db_xi[10];
            for (Uint j=0; j<10; ++j)
            {
                Db_xi[j]+=M*FE_P2CL::DHRef(j, p[0], p[1], p[2]);
            }
            for (Uint k=0; k<3; ++k)
            {
                Point3DCL tmpD= -LsgVel( p_world );
                for (Uint j=0; j<10; ++j)
                    tmpD+= veldof[j][k]*Db_xi[j];
                Dvals[i]+= inner_prod(tmpD, tmpD);
            }
        }
        L2_pr+= Quad3CL::Quad(pvals)*absdet;
        L2_vel+= Quad3CL::Quad(vals)*absdet;
        L2_Dvel+= Quad3CL::Quad(Dvals)*absdet;
    }
    const double X_norm= sqrt(L2_pr + L2_vel + L2_Dvel);
    L2_pr= sqrt(L2_pr);
    L2_vel= sqrt(L2_vel);
    L2_Dvel= sqrt(L2_Dvel);
    delete[] Dvals;
    delete[] vals;
    delete[] pvals;
    std::cerr << "|| (u_h, p_h) - (u, p) ||_X = " << X_norm
              << ", || u_h - u ||_L2 = " <<  L2_vel << ", || Du_h - Du ||_L2 = " << L2_Dvel
              << ", || p_h - p ||_L2 = " << L2_pr
              << std::endl;

/*
    // Compute the pressure-coefficient in direction of 1/sqrt(meas(Omega)), which eliminates
    // the allowed offset of the pressure by setting it to 0.
    double L1_pr= 0, L2_pr= 0, MW_pr= 0, vol= 0;
    P1EvalCL<double, const StokesPrBndDataCL, const VecDescCL>  pr(lsgpr, &_BndData.Pr, &_MG);
    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        const double volT= sit->GetVolume();
        double sum= 0;
        for(int i=0; i<4; ++i)
            sum+= pr.val(*sit->GetVertex(i)) - LsgPr(sit->GetVertex(i)->GetCoord());
        sum/= 120;
        sum+= 2./15.* (pr.val(*sit, .25, .25, .25) - LsgPr(GetBaryCenter(*sit)));
        MW_pr+= sum * volT*6.;
        vol+= volT;
    }
    const double c_pr= MW_pr / vol;
    std::cerr << "\nconstant pressure offset is " << c_pr<<", volume of cube is " << vol<<std::endl;;

    for (MultiGridCL::const_TriangVertexIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangVertexBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangVertexEnd(lvl);
         sit != send; ++sit)
    {
        diff= fabs( c_pr + LsgPr(sit->GetCoord()) - pr.val(*sit));
        norm2+= diff*diff;
        if (diff>maxdiff)
            maxdiff= diff;
        if (diff<mindiff)
            mindiff= diff;
    }
    norm2= ::sqrt(norm2 / lsgpr->Data.size() );

    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        double sum= 0, sum1= 0;
        for(int i=0; i<4; ++i)
        {
            diff= c_pr + LsgPr(sit->GetVertex(i)->GetCoord()) - pr.val(*sit->GetVertex(i));
            sum+= diff*diff; sum1+= fabs(diff);
        }
        sum/= 120;   sum1/= 120;
        diff= c_pr + LsgPr(GetBaryCenter(*sit)) - pr.val(*sit, .25, .25, .25);
        sum+= 2./15.*diff*diff;   sum1+= 2./15.*fabs(diff);
        L2_pr+= sum * sit->GetVolume()*6.;
        L1_pr+= sum1 * sit->GetVolume()*6.;
    }
    L2_pr= sqrt( L2_pr);
*/

    std::cerr << "Druck: Abweichung von der tatsaechlichen Loesung:\n"
              << "w-2-Norm= " << norm2 << std::endl
              << " L2-Norm= " << L2_pr << std::endl
              << "Differenz liegt zwischen " << mindiff << " und " << maxdiff << std::endl;
}


template <class MGB, class Coeff>
double StokesP2P1CL<MGB,Coeff>::ResidualErrEstimator(const TetraCL& t, const DiscPrSolCL& pr, const DiscVelSolCL& vel)
{
    double det;
    SMatrixCL<3,3> M;
    GetTrafoTr(M, det, t);
    const double absdet= fabs(det);
    const double vol= absdet/6.;
    Point3DCL cc; // unused; circumcenter of T dummy
    double hT; // radius of circumcircle of T
    circumcircle(t, cc, hT);
    // P_0(f) := (f, 1_T)_T * 1_T = int(f, T)/|T|
    const SVectorCL<3> P0f= Quad3CL::Quad(t, &Coeff::f)*6.;  //absdet/vol; // TODO: use absdet/vol = 6.

/*  stupid me: we do not need the perturbation terms....
    // torsion:  hT^2*(|| f - P0f ||_L2(T) )^2
    const Uint numpts= Quad3CL::GetNumPoints();
    double* vals= new double[numpts];
    for (Uint i=0; i<numpts; ++i)
    {
        const SVectorCL<3> tmp= Coeff::f( GetWorldCoord(t, Quad3CL::GetPoints()[i]) ) - P0f;
        vals[i]= inner_prod(tmp, tmp);
    }
    double err_sq= Quad3CL::Quad(vals)*absdet*hT*hT;
*/

    const Uint numpts= Quad3CL::GetNumPoints();
    double* vals= new double[numpts];
    double err_sq= 0.0;

    // Now eta_T^2:
    // Put velocity degrees of freedom in veldof
    std::vector< SVectorCL<3> > veldof;
    veldof.reserve(10);
    vel.GetDoF(t, veldof);
    // Put pressure degrees of freedom in prdof
    std::vector< double > prdof;
    prdof.reserve(4);
    pr.GetDoF(t, prdof);
    
    // || div(u_h) ||_L2(T) squared; the integrand is linear, thus we only need a quadrature-formula,
    // which is exact up to degree 2 (squares in L2-norm...); optimize this later.
    for (Uint i=0; i<numpts; ++i)
    {
        double tmp= 0.;
        for (Uint j=0; j<10; ++j)
        {
            tmp+= inner_prod( veldof[j], M*FE_P2CL::DHRef(j, Quad3CL::GetPoints()[i][0], Quad3CL::GetPoints()[i][1], Quad3CL::GetPoints()[i][2]) );
        }
        vals[i]= tmp*tmp;
    }
    err_sq+= Quad3CL::Quad(vals)*absdet;
    delete[] vals;

    // hT^2*int((-laplace(u) - grad(p) - P0f)^2, T) -- the sign of grad(p) is due to the implemented version of stokes eq.
    // the integrand is a constant for P2P1-discretisation...
    SVectorCL<3> tmp= -P0f;
    tmp-= M*(  prdof[0]*FE_P1CL::DH0Ref() + prdof[1]*FE_P1CL::DH1Ref()
             + prdof[2]*FE_P1CL::DH2Ref() + prdof[3]*FE_P1CL::DH3Ref() );
    for(Uint i=0; i<10; ++i)
    {
        // TODO: 1.0 stands for Stokes:GetCoeff().nu!! How do I obtain this here?
        tmp-= 1.0*veldof[i]*FE_P2CL::Laplace(i, M);
    }
    err_sq+= 4.*hT*hT*inner_prod(tmp, tmp)*vol;


    // Sum over all hF*int( [nu*nE*grad(u) + p*nE]_jumpoverface^2, face) not on the boundary
    const Uint lvl= vel.GetSolution()->RowIdx->TriangLevel;
    const Uint numpts2= FaceQuad2CL::GetNumPoints();
    vals= new double[numpts2];
    for (Uint f=0; f<NumFacesC; ++f)
    {
        const FaceCL& face= *t.GetFace(f);
        if ( !face.IsOnBoundary() )
        {
            const TetraCL& neigh= *face.GetNeighInTriang(&t, lvl);
            const Uint f_n= face.GetFaceNumInTetra(&neigh);
            // Put velocity degrees of freedom of neigh in veldof_n
            std::vector< SVectorCL<3> > veldof_n;
            veldof_n.reserve(10);
            vel.GetDoF(neigh, veldof_n);
            SMatrixCL<3,3> M_n;
            double ndet, dir;
            SVectorCL<3> n; // normal of the face; is the same on t and neigh,
                            // if the triangulation is consistently numbered.
            const double absdet2D= t.GetNormal(f, n, dir);
            GetTrafoTr(M_n, ndet, neigh);
            for (Uint pt=0; pt<numpts2; ++pt)
            {
                SVectorCL<3> tmp_me(0.);
                SVectorCL<3> tmp_n(0.);
                const SVectorCL<3> pos= FaceToTetraCoord(t, f, FaceQuad2CL::GetPoints()[pt]);
                const SVectorCL<3> pos_n= FaceToTetraCoord(neigh, f_n, FaceQuad2CL::GetPoints()[pt]);
                for (Uint i=0; i<10; ++i)
                {
                    const double gr= inner_prod( n, M*FE_P2CL::DHRef(i, pos[0], pos[1], pos[2]) );
                    const double ngr= inner_prod( n, M_n*FE_P2CL::DHRef(i, pos_n[0], pos_n[1], pos_n[2]) );
                    // TODO: 1.0 for nu; see above
                    tmp_me+= 1.0*veldof[i]*gr;
                    tmp_n+= 1.0*veldof_n[i]*ngr;
                }
                for (Uint i=0; i<4; ++i)
                {
                    tmp_me+= pr.val(t, pos[0], pos[1], pos[2])*n;
                    tmp_n+= pr.val(neigh, pos_n[0], pos_n[1], pos_n[2])*n;
                }
                vals[pt]= (tmp_me-tmp_n).norm_sq();
            }
            Point3DCL ccF; // Dummy
            double rF; // Radius of the Face
            circumcircle(t, f, ccF, rF);
            err_sq+= 2.*rF*FaceQuad2CL::Quad(vals)*absdet2D;
        }
    }
    delete[] vals;
    return err_sq;
}

//*********************************************************************
//**************P1Bubble-P1 - discretisation***************************
//*********************************************************************

//
template <class MGB, class Coeff>
void StokesP1BubbleP1CL<MGB,Coeff>::GetDiscError(vector_fun_ptr LsgVel, scalar_fun_ptr LsgPr) const
{
    Uint lvl= A.RowIdx->TriangLevel,
        vidx= A.RowIdx->Idx,
        pidx= B.RowIdx->Idx;
    VectorCL lsgvel(A.RowIdx->NumUnknowns);
    VectorCL lsgpr( B.RowIdx->NumUnknowns);

    for (MultiGridCL::const_TriangVertexIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangVertexBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangVertexEnd(lvl);
         sit != send; ++sit)
    {
        if (!_BndData.Vel.IsOnDirBnd(*sit))
        {
           for(int i=0; i<3; ++i)
               lsgvel[sit->Unknowns(vidx)[i]]= LsgVel(sit->GetCoord())[i];
        }
    }
    
    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {  // Tetras are never on the boundary.
       for(int i=0; i<3; ++i)
       {
            lsgvel[sit->Unknowns(vidx)[i]]= LsgVel( GetBaryCenter(*sit) )[i];
            // The coefficient of the bubble-function is not the value of the solution
            // in the barycenter, as the linear shape-functions contribute to the value
            // there.
            lsgvel[sit->Unknowns(vidx)[i]]-= .25*LsgVel( sit->GetVertex(0)->GetCoord() )[i];
            lsgvel[sit->Unknowns(vidx)[i]]-= .25*LsgVel( sit->GetVertex(1)->GetCoord() )[i];
            lsgvel[sit->Unknowns(vidx)[i]]-= .25*LsgVel( sit->GetVertex(2)->GetCoord() )[i];
            lsgvel[sit->Unknowns(vidx)[i]]-= .25*LsgVel( sit->GetVertex(3)->GetCoord() )[i];
       }
    }
    for (MultiGridCL::const_TriangVertexIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangVertexBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangVertexEnd(lvl);
         sit != send; ++sit)
        lsgpr[sit->Unknowns(pidx)[0]]= LsgPr(sit->GetCoord());

    std::cerr << "discretization error to check the system (x,y = continuos solution): "<<std::endl;
    VectorCL res= A.Data*lsgvel + transp_mul(B.Data,lsgpr)-b.Data; 
    std::cerr <<"|| Ax + BTy - f || = "<< res.norm()<<", max "<<res.supnorm()<<std::endl;
    VectorCL resB= B.Data*lsgvel - c.Data; 
    std::cerr <<"|| Bx - g || = "<< resB.norm()<<", max "<<resB.supnorm()<<std::endl;
}


/**************************************************************************************************
* member functions to handle with index descriptions
**************************************************************************************************/

//
template <class MGB, class Coeff>
void StokesP1BubbleP1CL<MGB,Coeff>::CreateNumberingPr(Uint level, IdxDescCL* idx)
// used for numbering of the Unknowns depending on the index IdxDesc[idxnum].
// sets up the description of the index idxnum in IdxDesc[idxnum],
// allocates memory for the Unknown-Indices on TriangLevel level und numbers them.
// Remark: expects, thatIdxDesc[idxnr].NumUnknownsVertex etc. are set.
{
    // set up the index description
    idx->TriangLevel = level;
    idx->NumUnknowns = 0;

    const Uint idxnum = idx->Idx;    // idx is the index in UnknownIdxCL

    // allocate space for indices; number unknowns in TriangLevel level; 
    CreateNumbOnVertex( idxnum, idx->NumUnknowns, idx->NumUnknownsVertex,
                        _MG.GetTriangVertexBegin(level), _MG.GetTriangVertexEnd(level),
                        _BndData.Pr );
}

//
template <class MGB, class Coeff>
void StokesP1BubbleP1CL<MGB,Coeff>::CreateNumberingVel(Uint level, IdxDescCL* idx)
// used for numbering of the Unknowns depending on the index IdxDesc[idxnum].
// sets up the description of the index idxnum in IdxDesc[idxnum],
// allocates memory for the Unknown-Indices on TriangLevel level und numbers them.
// Remark: expects, thatIdxDesc[idxnr].NumUnknownsVertex etc. are set.
{
    // set up the index description
    idx->TriangLevel = level;
    idx->NumUnknowns = 0;

    const Uint idxnum = idx->Idx;    // idx is the index in UnknownIdxCL

    // allocate space for indices; number unknowns in TriangLevel level
    CreateNumbOnVertex( idxnum, idx->NumUnknowns, idx->NumUnknownsVertex,
                        _MG.GetTriangVertexBegin(level), _MG.GetTriangVertexEnd(level),
                        _BndData.Vel );
    CreateNumbOnTetra( idxnum, idx->NumUnknowns, idx->NumUnknownsTetra,
                      _MG.GetTriangTetraBegin(level), _MG.GetTriangTetraEnd(level) );
}

//
template <class MGB, class Coeff>
void StokesP1BubbleP1CL<MGB,Coeff>::DeleteNumberingVel(IdxDescCL* idx)
{
    const Uint idxnum = idx->Idx;    // idx is the index in UnknownIdxCL
    const Uint level  = idx->TriangLevel;
    idx->NumUnknowns = 0;

    // delete memory allocated for indices
    DeleteNumbOnSimplex( idxnum, _MG.GetTriangVertexBegin(level), _MG.GetTriangVertexEnd(level) );
    DeleteNumbOnSimplex( idxnum, _MG.GetTriangTetraBegin(level), _MG.GetTriangTetraEnd(level) );
}

//
template <class MGB, class Coeff>
void StokesP1BubbleP1CL<MGB,Coeff>::DeleteNumberingPr(IdxDescCL* idx)
{
    const Uint idxnum = idx->Idx;    // idx is the index in UnknownIdxCL
    const Uint level  = idx->TriangLevel;
    idx->NumUnknowns = 0;

    // delete memory allocated for indices
    DeleteNumbOnSimplex( idxnum, _MG.GetTriangVertexBegin(level), _MG.GetTriangVertexEnd(level) );
}

//
inline double QuadGradP1Bubble(const SMatrixCL<3,3>& T, Uint i, Uint j)
// integral of the product of the gradients of shape function i and j
// This is exact, for i==j==4 see P1Bubble.txt - Maple is your friend :-)
// You still have to multiply the result of this call with fabs(det(T))!
{
    if (i<4 && j<4)
    { // both shape-functions are linear
        return inner_prod( T*FE_P1BubbleCL::DHRef(i), T*FE_P1BubbleCL::DHRef(j) )/6.;
    }
    else if (i==4 && j==4)
    { // the bubble-function in both args
        return 4096./2835.*( T(1,1)*T(1,1) + (T(1,1) + T(1,2))*(T(1,2) + T(1,3)) + T(1,3)*T(1,3)
                            +T(2,1)*T(2,1) + (T(2,1) + T(2,2))*(T(2,2) + T(2,3)) + T(2,3)*T(2,3)
                            +T(3,1)*T(3,1) + (T(3,1) + T(3,2))*(T(3,2) + T(3,3)) + T(3,3)*T(3,3) );
    }
    // linear function with bubble function; the integral of the gradients over the tetra vanishes
    // (proof via partial integration on tetra.)
    return 0.;
}

//
inline double GetB(Uint pr, Uint vel, const SMatrixCL<3,3>& T, Uint num)
{
    if (vel<4)
    { // both shape-functions are linear; evaluation of P1-functions in
      // the barycenter always yields .25;
        const SVectorCL<3> gradient( FE_P1CL::DHRef(vel) );
        return .25/6.*(T(num,0)*gradient[0] + T(num,1)*gradient[1] + T(num,2)*gradient[2]);
    }
    // i==4: we have the bubble-function for the velocity
    return 16./315.*(pr==0 ? T(num,0) + T(num,1) + T(num,2) : -T(num,pr) );
}


template <class MGB, class Coeff>
void StokesP1BubbleP1CL<MGB,Coeff>::SetupSystem(MatDescCL* matA, VelVecDescCL* vecA, MatDescCL* matB, VelVecDescCL* vecB) const
// Sets up the stiffness matrices and right hand sides
{
    vecA->Clear();
    vecB->Clear();
    
    const IdxT num_unks_vel= matA->RowIdx->NumUnknowns;
    const IdxT num_unks_pr=  matB->RowIdx->NumUnknowns;

    SparseMatBuilderCL<double> A(&matA->Data, num_unks_vel, num_unks_vel), 
                               B(&matB->Data, num_unks_pr,  num_unks_vel);
    VelVecDescCL& b   = *vecA;
    VelVecDescCL& c   = *vecB;
    const Uint lvl    = matA->RowIdx->TriangLevel;
    const Uint vidx   = matA->RowIdx->Idx,
               pidx   = matB->RowIdx->Idx;

    IdxT Numb[5], prNumb[4];
    bool IsOnDirBnd[5];
    
    const IdxT stride= 1;   // stride between unknowns on same simplex, which
                            // depends on numbering of the unknowns
                            
    std::cerr << "entering SetupSystem: " <<num_unks_vel<<" vels, "<<num_unks_pr<<" prs"<< std::endl;                            

    // fill value part of matrices
    SMatrixCL<3,3> T;
    double coup[5][5];
    double det, absdet;
    SVectorCL<3> tmp;

    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        GetTrafoTr(T,det,*sit);
        absdet= fabs(det);
        
        // collect some information about the verts and the tetra
        // and save it in Numb and IsOnDirBnd
        for(int i=0; i<4; ++i)
        {
            if(!(IsOnDirBnd[i]= _BndData.Vel.IsOnDirBnd( *sit->GetVertex(i) )))
                Numb[i]= sit->GetVertex(i)->Unknowns(vidx)[0];
            prNumb[i]= sit->GetVertex(i)->Unknowns(pidx)[0];
        }
        Numb[4]= sit->Unknowns(vidx)[0];
        IsOnDirBnd[4]= false; // the bubble-function is never on a boundary

        // compute all couplings between HatFunctions on tetra and verts
        for(int i=0; i<5; ++i)
            for(int j=0; j<=i; ++j)
            {
                // negative dot-product of the gradients
                coup[i][j]= _Coeff.nu * QuadGradP1Bubble(T, i, j)*absdet;
//                coup[i][j]+= P1BubbleDiscCL::Quad(*sit, &_Coeff.q, i, j)*absdet;
                coup[j][i]= coup[i][j];
            }

        for(int i=0; i<5; ++i)    // assemble row Numb[i]
            if (!IsOnDirBnd[i])  // vert/edge i is not on a Dirichlet boundary
            {
                for(int j=0; j<5; ++j)
                {
                    if (!IsOnDirBnd[j]) // vert/edge j is not on a Dirichlet boundary
                    {
                        A(Numb[i],          Numb[j])+=          coup[j][i]; 
                        A(Numb[i]+stride,   Numb[j]+stride)+=   coup[j][i]; 
                        A(Numb[i]+2*stride, Numb[j]+2*stride)+= coup[j][i]; 
                    }
                    else // coupling with vert/edge j on right-hand-side; j is always <4!
                    {
                        tmp= _BndData.Vel.GetDirBndValue(*sit->GetVertex(j));
                        b.Data[Numb[i]]-=          coup[j][i] * tmp[0];
                        b.Data[Numb[i]+stride]-=   coup[j][i] * tmp[1];
                        b.Data[Numb[i]+2*stride]-= coup[j][i] * tmp[2];
                    }
                }
                tmp= P1BubbleDiscCL::Quad(*sit, &_Coeff.f, i)*absdet;
                b.Data[Numb[i]]+=          tmp[0];
                b.Data[Numb[i]+stride]+=   tmp[1];
                b.Data[Numb[i]+2*stride]+= tmp[2];

                if ( i<4 && _BndData.Vel.IsOnNeuBnd(*sit->GetVertex(i)) ) // vert i is on Neumann boundary
                // i==4 is the bubble-function, which is never on any boundary.
                {
                    Uint face;
                    for (int f=0; f < 3; ++f)
                    {
                        face= FaceOfVert(i,f);
                        if ( sit->IsBndSeg(face))
                        {
                            tmp= Quad2D(*sit, face, i, _BndData.Vel.GetSegData(sit->GetBndIdx(face)).GetBndFun() );
                            b.Data[Numb[i]]+=          tmp[0];
                            b.Data[Numb[i]+stride]+=   tmp[1];
                            b.Data[Numb[i]+2*stride]+= tmp[2];
                        }
                    }
                }
            }
            
        // Setup B:   B(i,j) = int( psi_i * div( phi_j) )
        for(int vel=0; vel<5; ++vel)
        {
            if (!IsOnDirBnd[vel])
                for(int pr=0; pr<4; ++pr)
                {
                    B(prNumb[pr],Numb[vel])+=           GetB(pr, vel, T, 0)*absdet;
                    B(prNumb[pr],Numb[vel]+stride)+=    GetB(pr, vel, T, 1)*absdet;
                    B(prNumb[pr],Numb[vel]+2*stride)+=  GetB(pr, vel, T, 2)*absdet;
                }
            else // put coupling on rhs
            {
                tmp= _BndData.Vel.GetDirBndValue( *sit->GetVertex(vel));
                for(int pr=0; pr<4; ++pr)
                {
                    // numeric integration is exact: psi_i * div( phi_j) is of degree 2 !
                    c.Data[prNumb[pr]]-= GetB(pr, vel, T, 0)*absdet*tmp[0]
                                        +GetB(pr, vel, T, 1)*absdet*tmp[1]
                                        +GetB(pr, vel, T, 2)*absdet*tmp[2];
                }
            }
        }
    }
    std::cerr << "done: value part fill" << std::endl;

    A.Build();
    B.Build();
    std::cerr << matA->Data.num_nonzeros() << " nonzeros in A, "
              << matB->Data.num_nonzeros() << " nonzeros in B! " << std::endl;
}

template <class MGB, class Coeff>
void StokesP1BubbleP1CL<MGB,Coeff>::SetupMass(MatDescCL* matM) const
// Sets up the mass matrix
{
    const IdxT num_unks_pr=  matM->RowIdx->NumUnknowns;

    SparseMatBuilderCL<double> M(&matM->Data, num_unks_pr,  num_unks_pr);

    const Uint lvl    = matM->RowIdx->TriangLevel;
    const Uint pidx   = matM->RowIdx->Idx;

    IdxT prNumb[4];

    // compute all couplings between HatFunctions on verts:
    // I( i, j) = int ( psi_i*psi_j, T_ref) * absdet
    const double coupl_ii= 1./120. + 2./15./16.,
                 coupl_ij=           2./15./16.;

    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        const double absdet= sit->GetVolume()*6;
        
        for(int i=0; i<4; ++i)
            prNumb[i]= sit->GetVertex(i)->Unknowns(pidx)[0];

        for(int i=0; i<4; ++i)    // assemble row prNumb[i]
            for(int j=0; j<4; ++j)
                    M( prNumb[i], prNumb[j])+= (i==j ? coupl_ii : coupl_ij) * absdet;
    }
    M.Build();
}


template <class MGB, class Coeff>
double StokesP1BubbleP1CL<MGB,Coeff>::ResidualErrEstimator(const TetraCL& t, const DiscPrSolCL& pr, const DiscVelSolCL& vel)
{
    Uint lvl= vel.GetSolution()->RowIdx->TriangLevel;
    double err_sq= 0.0;
    double det, absdet, vol;
    SMatrixCL<3,3> M;

    GetTrafoTr(M, det, t);
    absdet= fabs(det);
    vol= absdet/6.;

    // || div(u_h,l) ||_L2 squared
    for (Uint i=0; i<4; ++i)
        for (Uint j=0; j<4; ++j)
        {
             err_sq+= inner_prod( vel.val(t.GetVertex(i)->GetCoord()), M*FE_P1BubbleCL::DHRef(i) )
                     *inner_prod( vel.val(t.GetVertex(j)->GetCoord()), M*FE_P1BubbleCL::DHRef(j) )/6.*absdet;
        }

    // || P_0(f) - grad(p_h) ||_L2 squared * Vol(T)
    const SVectorCL<3> P0f= Quad3CL::Quad(t, &Coeff::f)*absdet/sqrt(vol); // == *absdet/sqrt(vol)....
    const SVectorCL<3> gp= pr.val(t.GetVertex(0)->GetCoord())*M*FE_P1CL::DH0Ref()
                          +pr.val(t.GetVertex(1)->GetCoord())*M*FE_P1CL::DH1Ref()
                          +pr.val(t.GetVertex(2)->GetCoord())*M*FE_P1CL::DH2Ref()
                          +pr.val(t.GetVertex(3)->GetCoord())*M*FE_P1CL::DH3Ref();
    err_sq+= (P0f - gp).norm_sq()*vol*vol;

// TODO: non-null dirichlet-boundary values, neumann boundary
    // .5*sum_{E \in \partial T \cap \Omega} |E|*|| Sprung von u_h,l ||_L2,E^2
    SMatrixCL<3,4> vertval;
    for (Uint i=0; i<NumVertsC; ++i)
    {
        const SVectorCL<3> v= vel.val(t.GetVertex(i)->GetCoord());
        vertval(0,i)= v[0];
        vertval(1,i)= v[1];
        vertval(2,i)= v[2];
    }
    for (Uint f=0; f<NumFacesC; ++f)
    {
        const FaceCL& face= *t.GetFace(f);
        if ( !face.IsOnBoundary() )
        {
            const TetraCL& neigh= *face.GetNeighInTriang(&t, lvl);
            SMatrixCL<3,3> nM;
            double ndet, dir;
            SVectorCL<3> n;
            t.GetNormal(f, n, dir);
            GetTrafoTr(nM, ndet, neigh);
            const double absdet2D= FuncDet2D( face.GetVertex(1)->GetCoord() - face.GetVertex(0)->GetCoord(),
                                              face.GetVertex(2)->GetCoord() - face.GetVertex(0)->GetCoord() );
            SMatrixCL<3,4> nvertval;
            for (Uint i=0; i<NumVertsC; ++i)
            {
                const SVectorCL<3> v= vel.val(neigh.GetVertex(i)->GetCoord());
                nvertval(0,i)= v[0];
                nvertval(1,i)= v[1];
                nvertval(2,i)= v[2];
            }
            SVectorCL<3> grad_me(0.0);
            SVectorCL<3> grad_n(0.0);
            for (Uint i=0; i<NumVertsC; ++i)
            {
                const double gr= inner_prod( n, M*FE_P1CL::DHRef(i) );
                const double ngr= inner_prod( n, nM*FE_P1CL::DHRef(i) );
                grad_me[0]+= vertval(0,i)*gr;
                grad_me[1]+= vertval(1,i)*gr;
                grad_me[2]+= vertval(2,i)*gr;
                grad_n[0]+= nvertval(0,i)*ngr;
                grad_n[1]+= nvertval(1,i)*ngr;
                grad_n[2]+= nvertval(2,i)*ngr;
            }
            err_sq+= (grad_n - grad_me).norm_sq()/8.*absdet2D*absdet2D;
        }
    }
//    std::cerr << err_sq << '\n';
    return err_sq;
}

namespace // anonymous namespace
{
    typedef std::pair<const TetraCL*, double> Err_PairT;
    typedef std::vector<Err_PairT> Err_ContCL;

    struct AccErrCL :public std::binary_function<double, const Err_PairT, double>
    {
        double operator() (double init, const Err_PairT& ep) const
            { return init + ep.second;}
    };

    struct Err_Pair_GTCL :public std::binary_function<const Err_PairT, const Err_PairT, bool>
    {
        bool operator() (const Err_PairT& ep0, const Err_PairT& ep1)
        { return ep0.second > ep1.second; }
    };

} // end of anonymous namespace

template <class _TetraEst, class _ProblemCL>
void StokesDoerflerMarkCL<_TetraEst, _ProblemCL>::Init(const DiscPrSolCL& pr, const DiscVelSolCL& vel)
{
    MultiGridCL& mg= _Problem.GetMG();
    const Uint lvl= vel.GetSolution()->RowIdx->TriangLevel;

    double tmp;
    _InitGlobErr= 0.;
    for (MultiGridCL::TriangTetraIteratorCL sit=mg.GetTriangTetraBegin(lvl), send=mg.GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        tmp= _Estimator(*sit, pr, vel);
        _InitGlobErr+= tmp;
    }
    _InitGlobErr= sqrt(_InitGlobErr);
    _ActGlobErr= _InitGlobErr;
    if (_outp)
        *_outp << "ErrorEstimator is initialized now; initial error: " << _InitGlobErr
               << " We try to reduce by a factor of " << _RelReduction
               << " by marking the tetrahedrons with largest error-estimates, until the error marked" 
               << " is at least " << _Threshold << " of the actual global error." << std::endl;
}

template <class _TetraEst, class _ProblemCL>
bool StokesDoerflerMarkCL<_TetraEst, _ProblemCL>::Estimate(const DiscPrSolCL& pr, const DiscVelSolCL& vel)
{
    Err_ContCL err_est;
    const MultiGridCL& mg= _Problem.GetMG();
    const Uint lvl= vel.GetSolution()->RowIdx->TriangLevel;
//    const VecDescCL& lsg= *vel.GetSolution();

    _NumLastMarkedForRef= 0;
    _NumLastMarkedForDel= 0;
    for (MultiGridCL::const_TriangTetraIteratorCL sit=mg.GetTriangTetraBegin(lvl), send=mg.GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        const double localerr= _Estimator(*sit, pr, vel);
        err_est.push_back( std::make_pair(&*sit, localerr) );
    }
    const double globalerr_sq= std::accumulate(err_est.begin(), err_est.end(), 0.0, AccErrCL() );
    const double globalerr= sqrt(globalerr_sq);
    const double ref_threshold_sq= globalerr_sq*_Threshold*_Threshold;
    if (globalerr>=_InitGlobErr*_RelReduction && _DoMark)
    {
        std::sort( err_est.begin(), err_est.end(), Err_Pair_GTCL() );
        double akt_ref_err_sq= 0;
        for (Err_ContCL::iterator it= err_est.begin(), theend= err_est.end();
             it != theend && akt_ref_err_sq < ref_threshold_sq; ++it)
            if ( it->first->IsUnrefined() )
            {
                it->first->SetRegRefMark();
                akt_ref_err_sq+= it->second;
                ++_NumLastMarkedForRef;
            }
    }
    if (_outp)
        *_outp << "Estimated global |v|1 + ||pr||0-error: " << globalerr << ". This is a reduction of " << globalerr/_ActGlobErr
               << " compared to the last estimation. Marked " << _NumLastMarkedForRef
               << ", unmarked " << _NumLastMarkedForDel << " out of " << err_est.size() << " tetrahedrons, "
               << "which account for " << _Threshold << " of the global error."
               << std::endl;
    _ActGlobErr= globalerr;
    return _NumLastMarkedForRef || _NumLastMarkedForDel;
}


template <class MGB, class Coeff>
void StokesP1BubbleP1CL<MGB,Coeff>::CheckSolution(const VelVecDescCL* lsgvel, const VecDescCL* lsgpr, 
                                                  vector_fun_ptr LsgVel, scalar_fun_ptr LsgPr) const
{
    double diff, maxdiff=0, norm2= 0;
    Uint lvl=lsgvel->RowIdx->TriangLevel,
         vidx=lsgvel->RowIdx->Idx;
    
    VectorCL res1= A.Data*lsgvel->Data + transp_mul( B.Data, lsgpr->Data ) - b.Data;
    VectorCL res2= B.Data*lsgvel->Data - c.Data;

    std::cerr << "\nChecken der Loesung...\n";    
    std::cerr << "|| Ax + BTy - F || = " << res1.norm() << ", max. " << res1.supnorm() << std::endl;
    std::cerr << "||       Bx - G || = " << res2.norm() << ", max. " << res2.supnorm() << std::endl<<std::endl;
    
    P1BubbleEvalCL<SVectorCL<3>, const StokesVelBndDataCL, const VelVecDescCL>  vel(lsgvel, &_BndData.Vel, &_MG);
    double L1_div= 0, L2_div= 0;
    SMatrixCL<3,3> T;
    double det, absdet;

// We only use the linear part of the velocity-solution!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! 
    double* pvals= new double[Quad3CL::GetNumPoints()];
    double* pvals_sq= new double[Quad3CL::GetNumPoints()];
    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        GetTrafoTr(T,det,*sit);
        absdet= fabs(det);

        for (Uint i=0; i<Quad3CL::GetNumPoints(); ++i)
        {
            pvals[i]= fabs( inner_prod(T*FE_P1BubbleCL::DH0Ref(), vel.val(*sit->GetVertex(0)) )
                           +inner_prod(T*FE_P1BubbleCL::DH1Ref(), vel.val(*sit->GetVertex(1)) )
                           +inner_prod(T*FE_P1BubbleCL::DH2Ref(), vel.val(*sit->GetVertex(2)) )
                           +inner_prod(T*FE_P1BubbleCL::DH3Ref(), vel.val(*sit->GetVertex(3)) ) );
            pvals_sq[i]= pvals[i]*pvals[i];
        }
        L1_div+= Quad3CL::Quad(pvals)*absdet;
        L2_div+= Quad3CL::Quad(pvals_sq)*absdet;
    }
    L2_div= ::sqrt(L2_div);
    std::cerr << "|| div x ||_L1 = " << L1_div << std::endl;
    std::cerr << "|| div x ||_L2 = " << L2_div << std::endl << std::endl;
    delete[] pvals_sq; delete[] pvals;
    
    Uint countverts=0;
    for (MultiGridCL::const_TriangVertexIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangVertexBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangVertexEnd(lvl);
         sit != send; ++sit)
    {
        if (!_BndData.Vel.IsOnDirBnd(*sit))
        {
           ++countverts;
           for(int i=0; i<3; ++i)
           {
               diff= fabs( LsgVel(sit->GetCoord())[i] - lsgvel->Data[sit->Unknowns(vidx)[i]] );
               norm2+= diff*diff;
               if (diff>maxdiff)
               {
                   maxdiff= diff;
               }
           }
        }
    }
    norm2= ::sqrt(norm2/countverts);

    Point3DCL L1_vel(0.0), L2_vel(0.0);
    SVectorCL<3>* vvals= new SVectorCL<3>[Quad3CL::GetNumPoints()];
    SVectorCL<3>* vvals_sq= new SVectorCL<3>[Quad3CL::GetNumPoints()];
    for(MultiGridCL::const_TriangTetraIteratorCL sit= const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send= const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
        sit != send; ++sit)
    {
        GetTrafoTr(T,det,*sit);
        absdet= fabs(det);
//        Point3DCL sum(0.0), diff, Diff[5];
        for (Uint i=0; i<Quad3CL::GetNumPoints(); ++i)
        {
            const Point3DCL& pt= Quad3CL::GetPoints()[i];
            vvals[i]= fabs(LsgVel(GetWorldCoord(*sit, pt)) - vel.lin_val(*sit, pt[0], pt[1], pt[2]));
            vvals_sq[i]= vvals[i]*vvals[i];
        }
        L1_vel+= Quad3CL::Quad(vvals)*absdet;
        L2_vel+= Quad3CL::Quad(vvals_sq)*absdet;
    }
    L2_vel= sqrt(L2_vel);
    delete[] vvals_sq; delete[] vvals;
    std::cerr << "Geschwindigkeit: Abweichung von der tatsaechlichen Loesung:\n"
              << "w-2-Norm= " << norm2 << std::endl
              << " L2-Norm= (" << L2_vel[0]<<", "<<L2_vel[1]<<", "<<L2_vel[2]<<")" << std::endl
              << " L1-Norm= (" << L1_vel[0]<<", "<<L1_vel[1]<<", "<<L1_vel[2]<<")" << std::endl
              << "max-Norm= " << maxdiff << std::endl;

    norm2= 0; maxdiff= 0; double mindiff= 1000;

    // Compute the pressure-coefficient in direction of 1/sqrt(meas(Omega)), which eliminates
    // the allowed offset of the pressure by setting it to 0.
    double L1_pr= 0, L2_pr= 0, MW_pr= 0, vol= 0;
    P1EvalCL<double, const StokesPrBndDataCL, const VecDescCL>  pr(lsgpr, &_BndData.Pr, &_MG);
    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        double sum= 0;
        for(int i=0; i<4; ++i)
            sum+= pr.val(*sit->GetVertex(i)) - LsgPr(sit->GetVertex(i)->GetCoord());
        sum/= 120;
        sum+= 2./15.* (pr.val(*sit, .25, .25, .25) - LsgPr(GetBaryCenter(*sit)));
        MW_pr+= sum * sit->GetVolume()*6.;
        vol+= sit->GetVolume();
    }
    const double c_pr= MW_pr / vol;
    std::cerr << "\nconstant pressure offset is " << c_pr<<", volume of cube is " << vol<<std::endl;;

    for (MultiGridCL::const_TriangVertexIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangVertexBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangVertexEnd(lvl);
         sit != send; ++sit)
    {
        diff= fabs( c_pr + LsgPr(sit->GetCoord()) - pr.val(*sit));
        norm2+= diff*diff;
        if (diff>maxdiff)
            maxdiff= diff;
        if (diff<mindiff)
            mindiff= diff;
    }
    norm2= ::sqrt(norm2 / lsgpr->Data.size() );

    for (MultiGridCL::const_TriangTetraIteratorCL sit=const_cast<const MultiGridCL&>(_MG).GetTriangTetraBegin(lvl), send=const_cast<const MultiGridCL&>(_MG).GetTriangTetraEnd(lvl);
         sit != send; ++sit)
    {
        double sum= 0, sum1= 0;
        for(int i=0; i<4; ++i)
        {
            diff= c_pr + LsgPr(sit->GetVertex(i)->GetCoord()) - pr.val(*sit->GetVertex(i));
            sum+= diff*diff; sum1+= fabs(diff);
        }
        sum/= 120;   sum1/= 120;
        diff= c_pr + LsgPr(GetBaryCenter(*sit)) - pr.val(*sit, .25, .25, .25);
        sum+= 2./15.*diff*diff;   sum1+= 2./15.*fabs(diff);
        L2_pr+= sum * sit->GetVolume()*6.;
        L1_pr+= sum1 * sit->GetVolume()*6.;
    }
    L2_pr= sqrt( L2_pr);


    std::cerr << "Druck: Abweichung von der tatsaechlichen Loesung:\n"
              << "w-2-Norm= " << norm2 << std::endl
              << " L2-Norm= " << L2_pr << std::endl
              << " L1-Norm= " << L1_pr << std::endl
              << "Differenz liegt zwischen " << mindiff << " und " << maxdiff << std::endl;
}

inline double norm_L2_sq(const TetraCL& t, scalar_fun_ptr coeff)
{
    const double f0= coeff(t.GetVertex(0)->GetCoord());
    const double f1= coeff(t.GetVertex(1)->GetCoord());
    const double f2= coeff(t.GetVertex(2)->GetCoord());
    const double f3= coeff(t.GetVertex(3)->GetCoord());
    const double fb= coeff(GetBaryCenter(t));
    return (f0*f0 + f1*f1 + f2*f2 + f3*f3)/120. + 2./15.*fb*fb;
    
}

} // end of namespace DROPS

#endif

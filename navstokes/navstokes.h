//**************************************************************************
// File:    navstokes.h                                                    *
// Content: classes that constitute the navier-stokes-problem              *
// Author:  Sven Gross, Joerg Peters, Volker Reichelt, IGPM RWTH Aachen    *
// Version: 0.1                                                            *
// History: begin - April, 30 2001                                         *
//**************************************************************************

#ifndef _NAVSTOKES_H_
#define _NAVSTOKES_H_

#include "stokes/stokes.h"


namespace DROPS
{

template <class MGB, class Coeff>
class NavierStokesP2P1CL : public StokesP2P1CL<MGB, Coeff>
{
  private:
    typedef StokesP2P1CL<MGB, Coeff> BaseCL;
    
  public:
    typedef MGB                           MultiGridBuilderCL;
    typedef Coeff                         CoeffCL;
    typedef typename BaseCL::BndDataCL    BndDataCL;
    typedef typename BaseCL::VelVecDescCL VelVecDescCL;
  
    MatDescCL    N;
    VelVecDescCL cplN;
  
    NavierStokesP2P1CL(const MultiGridBuilderCL& mgb, const CoeffCL& coeff, const BndDataCL& bdata)
        : StokesP2P1CL<MGB, Coeff>(mgb, coeff, bdata) {}  
    NavierStokesP2P1CL(MultiGridCL& mg, const CoeffCL& coeff, const BndDataCL& bdata)
        : StokesP2P1CL<MGB, Coeff>(mg, coeff, bdata) {}  

    // Set up matrix for nonlinearity
    void SetupNonlinear(MatDescCL*, const VelVecDescCL*, VelVecDescCL*) const;

    // Check system and computed solution
    void GetDiscError (vector_fun_ptr LsgVel, scalar_fun_ptr LsgPr);
    void CheckSolution(const VelVecDescCL*, const VecDescCL*, vector_fun_ptr, scalar_fun_ptr);
};


} // end of namespace DROPS

//======================================
//  definition of template functions 
//======================================
#include "navstokes/navstokes.tpp"


#endif

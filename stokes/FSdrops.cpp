#include "geom/multigrid.h"
#include "out/output.h"
#include "geom/builder.h"
#include "stokes/instatstokes.h"
#include <fstream>


namespace DROPS // for Strategy
{

struct StatStokesCL
{
    static SVectorCL<3> LsgVel(const Point3DCL& p, double)
    {
        SVectorCL<3> ret;
        ret[0]=    sin(p[0])*sin(p[1])*sin(p[2]);
        ret[1]=  - cos(p[0])*cos(p[1])*sin(p[2]);
        ret[2]= 2.*cos(p[0])*sin(p[1])*cos(p[2]);
        return ret/3.;
    }

    static double LsgPr(const Point3DCL& p, double)
    {
        return -cos(p[0])*sin(p[1])*sin(p[2]);
    }

    // du/dt + q*u - nu*laplace u - Dp = f
    //                           div u = 0
    class StokesCoeffCL
    {
      public:
        static double q(const Point3DCL&) { return 0.0; }
        static SVectorCL<3> f(const Point3DCL& p, double)
            { SVectorCL<3> ret(0.0); ret[2]= 3*cos(p[0])*sin(p[1])*cos(p[2]); return ret; }
        const double nu;

        StokesCoeffCL() : nu(1.0) {}
    };
    static StokesCoeffCL Coeff;
};

StatStokesCL::StokesCoeffCL StatStokesCL::Coeff;


struct InstatStokes2CL
{
    static SVectorCL<3> LsgVel(const Point3DCL& p, double t)
    {
        SVectorCL<3> ret;
        ret[0]=    sin(p[0])*sin(p[1])*sin(p[2])*t*t;
        ret[1]=  - cos(p[0])*cos(p[1])*sin(p[2])*t*t;
        ret[2]= 2.*cos(p[0])*sin(p[1])*cos(p[2])*t*t;
        return ret/3.;
    }

    static double LsgPr(const Point3DCL& p, double t)
    {
        return -cos(p[0])*sin(p[1])*sin(p[2])*t*t;
    }

    // du/dt + q*u - nu*laplace u - Dp = f
    //                           div u = 0
    class StokesCoeffCL
    {
      public:
        static double q(const Point3DCL&) { return 0.0; }
        static SVectorCL<3> f(const Point3DCL& p, double t)
        { 
            SVectorCL<3> ret;
            ret[0]=       2/3*t*sin(p[0])*sin(p[1])*sin(p[2]);
            ret[1]=      -2/3*t*cos(p[0])*cos(p[1])*sin(p[2]);
            ret[2]= (4/3+3*t)*t*cos(p[0])*sin(p[1])*cos(p[2]);
            return ret;
        }
        const double nu;

        StokesCoeffCL() : nu(1.0) {}
    };
    static StokesCoeffCL Coeff;
};

InstatStokes2CL::StokesCoeffCL InstatStokes2CL::Coeff;


struct InstatStokes3CL
{
    static SVectorCL<3> LsgVel(const Point3DCL&, double t)
    {
        return SVectorCL<3>(-t);
    }

    static double LsgPr(const Point3DCL& p, double)
    {
        return p[0]+p[1]+p[2];
    }

    // du/dt + q*u - nu*laplace u - Dp = f
    //                           div u = 0
    class StokesCoeffCL
    {
      public:
        static double q(const Point3DCL&) { return 0.0; }
        static SVectorCL<3> f(const Point3DCL&, double)
        { 
            return SVectorCL<3>(-2.);
        }
        const double nu;

        StokesCoeffCL() : nu(1.0) {}
    };
    static StokesCoeffCL Coeff;
};

InstatStokes3CL::StokesCoeffCL InstatStokes3CL::Coeff;


struct InstatStokesCL
{
    static inline SVectorCL<3> helper( const Point3DCL& p)
    {
        SVectorCL<3> ret;
        ret[0]=     cos(p[0])*sin(p[1])*sin(p[2]);
        ret[1]=     sin(p[0])*cos(p[1])*sin(p[2]);
        ret[2]= -2.*sin(p[0])*sin(p[1])*cos(p[2]);
        return ret;
    }
    
    static SVectorCL<3> LsgVel(const Point3DCL& p, double t)
    {
        return t*helper(p);
    }

    static double LsgPr(const Point3DCL&, double)
    {
        return 0;
    }

    // du/dt + q*u - nu*laplace u - Dp = f
    //                           div u = 0
    class StokesCoeffCL
    {
      public:
        static double q(const Point3DCL&) { return 0.0; }
        static SVectorCL<3> f(const Point3DCL& p, double t)
            { return (1 + 3*t)*helper(p); }
        const double nu;

        StokesCoeffCL() : nu(1.0) {}
    };
    static StokesCoeffCL Coeff;
};

InstatStokesCL::StokesCoeffCL InstatStokesCL::Coeff;

} // end of namespace DROPS


/***************************************************************************
*  Choose here the PDE you want DROPS to solve!
***************************************************************************/

typedef DROPS::InstatStokesCL MyPDE;



namespace DROPS
{

void SchurNoPc( const FracStepMatrixCL& M, const MatrixCL& B, 
                VectorCL& u, VectorCL& p, const VectorCL& b, const VectorCL& c,
                const double inner_tol, const double outer_tol, const Uint max_iter, double dt)
// solve:       S*q = B*(M^-1)*b - c
//              M*u = b - BT*q
//                q = dt*p 
{
    p*=dt;
    VectorCL rhs= -c;
    {
        double tol= inner_tol;
        int iter= max_iter;        
        VectorCL tmp( u.size());
        CG(M, tmp, b, iter, tol);
        std::cerr << "Iterationen: " << iter << "    Norm des Residuums: " << tol << std::endl;
        rhs+= B*tmp;
    }
    std::cerr << "rhs has been set! Now solving pressure..." << std::endl;
    int iter= max_iter;   
    double tol= outer_tol;     
    //        PCG(A->Data, new_x->Data, b->Data, pc, max_iter, tol);
    CG( SchurComplNoPcMatrixCL( M, B, inner_tol), p, rhs, iter, tol);
    std::cerr << "Iterationen: " << iter << "    Norm des Residuums: " << tol << std::endl;

    std::cerr << "pressure has been solved! Now solving velocities..." << std::endl;
    tol= outer_tol;
    iter= max_iter;        
    CG(M, u, b - transp_mul(B, p), iter, tol);
    std::cerr << "Iterationen: " << iter << "    Norm des Residuums: " << tol << std::endl;
    p/=dt;
    std::cerr << "-----------------------------------------------------" << std::endl;
}

template<class PreCondT>
void Schur( const MatrixCL& M, const PreCondT& pc, const MatrixCL& B, 
            VectorCL& u, VectorCL& p, const VectorCL& b, const VectorCL& c,
            const double inner_tol, const double outer_tol, const Uint max_iter, const double omega, double dt)
// solve:       S*q = B*(M^-1)*b - c
//              M*u = b - BT*q
//                q = dt*p 
{
    p*= dt;
    VectorCL rhs= -c;
    {
        double tol= inner_tol;
        int iter= max_iter;        
        VectorCL tmp( u.size());
        PCG(M, tmp, b, pc, iter, tol);
        std::cerr << "Iterationen: " << iter << "    Norm des Residuums: " << tol << std::endl;
        rhs+= B*tmp;
    }
    std::cerr << "rhs has been set! Now solving pressure..." << std::endl;
    int iter= max_iter;   
    double tol= outer_tol;     
    //        PCG(A->Data, new_x->Data, b->Data, pc, max_iter, tol);
    CG( SchurComplMatrixCL( M, B, inner_tol, omega), p, rhs, iter, tol);
    std::cerr << "Iterationen: " << iter << "    Norm des Residuums: " << tol << std::endl;

    std::cerr << "pressure has been solved! Now solving velocities..." << std::endl;
    tol= outer_tol;
    iter= max_iter;        
    PCG(M, u, b - transp_mul(B, p), pc, iter, tol);
    std::cerr << "Iterationen: " << iter << "    Norm des Residuums: " << tol << std::endl;
    p/= dt;
    std::cerr << "-----------------------------------------------------" << std::endl;
}

template<class MGB, class Coeff>
void Strategy(InstatStokesP2P1CL<MGB,Coeff>& Stokes, double omega, double inner_iter_tol, Uint maxStep)
// flow control
{
    typedef typename InstatStokesP2P1CL<MGB,Coeff>::VelVecDescCL VelVecDescCL;
    
    MultiGridCL& MG= Stokes.GetMG();

    IdxDescCL  loc_vidx, loc_pidx;
    IdxDescCL* vidx1= &Stokes.vel_idx;
    IdxDescCL* pidx1= &Stokes.pr_idx;
    IdxDescCL* vidx2= &loc_vidx;
    IdxDescCL* pidx2= &loc_pidx;
//    IdxDescCL* err_idx= &_err_idx;
    VecDescCL     loc_p;
    VelVecDescCL  loc_v;
    VelVecDescCL* v1= &Stokes.v;
    VelVecDescCL* v2= &loc_v;
    VecDescCL*    p1= &Stokes.p;
    VecDescCL*    p2= &loc_p;
    VelVecDescCL* b= &Stokes.b;
    VelVecDescCL* c= &Stokes.c;
//    VecDescCL* err= &_err;
    MatDescCL* A= &Stokes.A;
    MatDescCL* B= &Stokes.B;
    MatDescCL* I= &Stokes.M;
    Uint step= 0;
//    bool new_marks;
//    double akt_glob_err;

    vidx1->Set(0, 3, 3, 0, 0);
    vidx2->Set(1, 3, 3, 0, 0);
    pidx1->Set(2, 1, 0, 0, 0);
    pidx2->Set(3, 1, 0, 0, 0);
    
    TimerCL time;
//    err_idx->Set(5, 0, 0, 0, 1);
    do
    {
//        akt_glob_err= glob_err;
        MarkAll( MG);
        MG.Refine();
        Stokes.CreateNumberingVel(MG.GetLastLevel(), vidx1);    
        Stokes.CreateNumberingPr(MG.GetLastLevel(), pidx1);    
        b->SetIdx(vidx1);
        c->SetIdx(pidx1);
        p1->SetIdx(pidx1);
        v1->SetIdx(vidx1);
        std::cerr << "Anzahl der Druck-Unbekannten: " << p2->Data.size() << ", "
                  << p1->Data.size() << std::endl;
        std::cerr << "Anzahl der Geschwindigkeitsunbekannten: " << v2->Data.size() << ", "
                  << v1->Data.size() << std::endl;
/*        if (p2->RowIdx)
        {
            const StokesBndDataCL& BndData= Stokes.GetBndData();
            P1EvalCL<double, const StokesPrBndDataCL, const VecDescCL>  pr2(p2, &BndData.Pr, &MG);
            P1EvalCL<double, const StokesPrBndDataCL, VecDescCL>        pr1(p1, &BndData.Pr, &MG);
            P2EvalCL<SVectorCL<3>, const StokesVelBndDataCL, const VelVecDescCL> vel2(v2, &BndData.Vel, &MG);
            P2EvalCL<SVectorCL<3>, const StokesVelBndDataCL, VelVecDescCL>       vel1(v1, &BndData.Vel, &MG);
            Interpolate(pr1, pr2);
            Interpolate(vel1, vel2);
//            CheckSolution(v1,p1,&LsgVel,&LsgPr);
            v2->Reset();
            p2->Reset();
        }
*/        A->SetIdx(vidx1, vidx1);
        B->SetIdx(pidx1, vidx1);
        I->SetIdx(vidx1, vidx1);
        time.Reset();
/*        VelVecDescCL w1, w2;
        w1.SetIdx(vidx1);
        w2.SetIdx(vidx1);
        Stokes.SetupInstatSystem(A, &w1, B, c); // time independent part
        Stokes.SetupInstatRhs( &w2, 0);
        w1.Data+= w2.Data;
        Stokes.SetupSystem(A, b, B, c, 0);
        std::cerr << "Abweichung maximal " << (b->Data - w1.Data).norm() << std::endl;
*/        Stokes.SetupInstatSystem(A, B, I); // time independent part
        time.Stop();
        std::cerr << time.GetTime() << " seconds for setting up all systems!" << std::endl;
        Stokes.InitVel( v1, &MyPDE::LsgVel);
        time.Reset();
        A->Data * v1->Data;
        time.Stop();
        std::cerr << " A*x took " << time.GetTime() << " seconds!" << std::endl;
        time.Reset();
        transp_mul( A->Data, v1->Data);
        time.Stop();
        std::cerr << "AT*x took " << time.GetTime() << " seconds!" << std::endl;
        
        Uint meth;
        double dt, t= 0;

        std::cerr << "\nDelta t = "; std::cin >> dt;
        
        const double theta= 1 - sqrt(2.)/2,
                     alpha= (1 - 2*theta)/(1 - theta);
        const double eta= alpha*theta*dt;
        double macrostep;
        
        std::cerr << "which method? 0=Schur, 1=SchurNoPc, 2=impl./expl. Euler > "; std::cin >> meth;
        time.Reset();
        switch(meth)
        {
        case 1: {
            FracStepMatrixCL M( I->Data, A->Data, eta);
            VectorCL rhs( v1->Data.size());
                     
            double outer_tol, old_time= 0;
            std::cerr << "tol = "; std::cin >> outer_tol;

            VelVecDescCL *cplA= new VelVecDescCL,
                         *cplI= new VelVecDescCL,
                         *old_cplA= new VelVecDescCL,
                         *old_cplI= new VelVecDescCL,
                         *f= b;

            cplA->SetIdx(vidx1);
            cplI->SetIdx(vidx1);
            old_cplA->SetIdx(vidx1);
            old_cplI->SetIdx(vidx1);
            
            do
            {
                time.Start();
                
                macrostep= theta*dt;
                Stokes.SetupInstatRhs( cplA, c, cplI, t+macrostep, f, t);
                rhs= A->Data * v1->Data;
                rhs*= -(1-alpha)*macrostep;
                rhs+= I->Data*v1->Data + macrostep*f->Data;
                rhs+= cplI->Data - old_cplI->Data + macrostep * ( alpha*cplA->Data + (1-alpha)*old_cplA->Data );
                SchurNoPc( M, B->Data, v1->Data, p1->Data, rhs, c->Data, inner_iter_tol, outer_tol, 1000, macrostep);
                std::swap( cplA, old_cplA);
                std::swap( cplI, old_cplI);

                macrostep= (1-2*theta)*dt;
                Stokes.SetupInstatRhs( cplA, c, cplI, t+(1-theta)*dt, f, t+(1-theta)*dt);
                rhs= A->Data * v1->Data;
                rhs*= -alpha*macrostep;
                rhs+= I->Data*v1->Data + macrostep*f->Data;
                rhs+= cplI->Data - old_cplI->Data + macrostep * ( alpha*old_cplA->Data + (1-alpha)*cplA->Data );
                SchurNoPc( M, B->Data, v1->Data, p1->Data, rhs, c->Data, inner_iter_tol, outer_tol, 1000, macrostep);
                std::swap( cplA, old_cplA);
                std::swap( cplI, old_cplI);

                macrostep= theta*dt;
                Stokes.SetupInstatRhs( cplA, c, cplI, t+dt, f, t+(1-theta)*dt);
                rhs= A->Data * v1->Data;
                rhs*= -(1-alpha)*macrostep;
                rhs+= I->Data*v1->Data + macrostep*f->Data;
                rhs+= cplI->Data - old_cplI->Data + macrostep * ( alpha*cplA->Data + (1-alpha)*old_cplA->Data );
                SchurNoPc( M, B->Data, v1->Data, p1->Data, rhs, c->Data, inner_iter_tol, outer_tol, 1000, macrostep);
                std::swap( cplA, old_cplA);
                std::swap( cplI, old_cplI);

                t+= dt;
                
                time.Stop();
//        Stokes.SetupInstatRhs( b, t);
//        Stokes.CheckSolution(v1, p1, &MyPDE::LsgVel, &MyPDE::LsgPr);
                std::cerr << "Zeitschritt t = " << t << " dauerte " << time.GetTime()-old_time << " sec\n" << std::endl;
                old_time= time.GetTime();
            } while (fabs(t - 1.)>DoubleEpsC);

            delete cplA;
            delete cplI;
            delete old_cplA;
            delete old_cplI;
        } break;

        case 0: {
            SsorPcCL<VectorCL, double> pc(omega);
            VectorCL rhs( v1->Data.size());
                     
            MatrixCL M;
            ConvexComb( M, 1., I->Data, eta, A->Data);

            double outer_tol, old_time= 0;
            std::cerr << "tol = "; std::cin >> outer_tol;
            
            VelVecDescCL *cplA= new VelVecDescCL,
                         *cplI= new VelVecDescCL,
                         *old_cplA= new VelVecDescCL,
                         *old_cplI= new VelVecDescCL,
                         *f= b;

            cplA->SetIdx(vidx1);
            cplI->SetIdx(vidx1);
            old_cplA->SetIdx(vidx1);
            old_cplI->SetIdx(vidx1);

            Stokes.SetupInstatRhs( old_cplA, c, old_cplI, t, f, t);
            do
            {
                time.Start();
                
                macrostep= theta*dt;
                Stokes.SetupInstatRhs( cplA, c, cplI, t+macrostep, f, t);
                rhs= A->Data * v1->Data;
                rhs*= -(1-alpha)*macrostep;
                rhs+= I->Data*v1->Data + macrostep*f->Data;
                rhs+= cplI->Data - old_cplI->Data + macrostep * ( alpha*cplA->Data + (1-alpha)*old_cplA->Data );
                Schur( M, pc, B->Data, v1->Data, p1->Data, rhs, c->Data, inner_iter_tol, outer_tol, 200, omega, macrostep);
                std::swap( cplA, old_cplA);
                std::swap( cplI, old_cplI);

                macrostep= (1-2*theta)*dt;
                Stokes.SetupInstatRhs( cplA, c, cplI, t+(1-theta)*dt, f, t+(1-theta)*dt);
                rhs= A->Data * v1->Data;
                rhs*= -alpha*macrostep;
                rhs+= I->Data*v1->Data + macrostep*f->Data;
                rhs+= cplI->Data - old_cplI->Data + macrostep * ( alpha*old_cplA->Data + (1-alpha)*cplA->Data );
                Schur( M, pc, B->Data, v1->Data, p1->Data, rhs, c->Data, inner_iter_tol, outer_tol, 200, omega, macrostep);
                std::swap( cplA, old_cplA);
                std::swap( cplI, old_cplI);

                macrostep= theta*dt;
                Stokes.SetupInstatRhs( cplA, c, cplI, t+dt, f, t+(1-theta)*dt);
                rhs= A->Data * v1->Data;
                rhs*= -(1-alpha)*macrostep;
                rhs+= I->Data*v1->Data + macrostep*f->Data;
                rhs+= cplI->Data - old_cplI->Data + macrostep * ( alpha*cplA->Data + (1-alpha)*old_cplA->Data );
                Schur( M, pc, B->Data, v1->Data, p1->Data, rhs, c->Data, inner_iter_tol, outer_tol, 200, omega, macrostep);
                std::swap( cplA, old_cplA);
                std::swap( cplI, old_cplI);

                t+= dt;
                
                time.Stop();
//        Stokes.SetupInstatRhs( b, t);
//        Stokes.CheckSolution(v1, p1, &MyPDE::LsgVel, &MyPDE::LsgPr, t);
                std::cerr << "Zeitschritt t = " << t << " dauerte " << time.GetTime()-old_time << " sec\n" << std::endl;
                old_time= time.GetTime();
            } while (fabs(t - 1.)>DoubleEpsC);

            delete cplA;
            delete cplI;
            delete old_cplA;
            delete old_cplI;
        } break;

        case 2: {
            SsorPcCL<VectorCL, double> pc(omega);
            VectorCL v( v1->Data.size());
                     
            double outer_tol, theta, old_time= 0;
            std::cerr << "theta = "; std::cin >> theta;
            std::cerr << "tol = "; std::cin >> outer_tol;
            
            MatrixCL M;
            ConvexComb( M, 1., I->Data, theta*dt, A->Data);

            VelVecDescCL* old_b= new VelVecDescCL;
            VelVecDescCL* cplI= new VelVecDescCL;
            VelVecDescCL* old_cplI= new VelVecDescCL;
                          
            old_b->SetIdx( b->RowIdx);
            cplI->SetIdx( b->RowIdx);
            old_cplI->SetIdx( b->RowIdx);
            Stokes.SetupInstatRhs( old_b, c, old_cplI, t, old_b, t);
            do
            {
                time.Start();
                
                Stokes.SetupInstatRhs( b, c, cplI, t+dt, b, t+dt);
                v= A->Data * v1->Data;
                v*= (theta-1)*dt;
                v+= I->Data*v1->Data + cplI->Data - old_cplI->Data
                  + dt*( theta*b->Data + (1-theta)*old_b->Data);
                Schur( M, pc, B->Data, v1->Data, p1->Data, v, c->Data, inner_iter_tol, outer_tol, 200, omega, dt);

                t+= dt;
                std::swap( b, old_b);
                std::swap( cplI, old_cplI);

                time.Stop();
//        Stokes.SetupSystem(A,b,B,c,t);
//        Stokes.CheckSolution(v1, p1, &MyPDE::LsgVel, &MyPDE::LsgPr, t);
                std::cerr << "Zeitschritt t = " << t << " dauerte " << time.GetTime()-old_time << " sec\n" << std::endl;
                old_time= time.GetTime();
            } while (fabs(t - 1.)>DoubleEpsC);
            
            if (old_b == &Stokes.b)
                std::swap( old_b, b);
            delete old_b;
            delete cplI;
            delete old_cplI;
        } break;
        
        default: std::cerr << "ERROR: No such method!" << std::endl;
        
        } // switch

/*        else
        {
            max_iter= 5000;
            double tau;
            Uint inner_iter;
            std::cerr << "tol = "; std::cin >> tol;
            std::cerr << "tau = "; std::cin >> tau;
            std::cerr << "#PCG steps = "; std::cin >> inner_iter;
            time.Start();
            Uzawa( A->Data, B->Data, v1->Data, p1->Data, b->Data, c->Data, tau, max_iter, tol, inner_iter);
//            Uzawa2( A->Data, B->Data, v1->Data, p1->Data, b->Data, c->Data, max_iter, tol, inner_iter, inner_iter_tol);
            time.Stop();
            std::cerr << "Iterationen: " << max_iter << "    Norm des Residuums: " << tol << std::endl;
        }*/
        std::cerr << "Das Verfahren brauchte "<<time.GetTime()<<" Sekunden.\n";
        Stokes.SetupSystem(A,b,B,c,t);
        Stokes.CheckSolution(v1, p1, &MyPDE::LsgVel, &MyPDE::LsgPr, t);
        A->Reset();
        B->Reset();
        b->Reset();
        c->Reset();
//        std::cerr << "Loesung Druck: " << p1->Data << std::endl;
//        CreateNumbering(new_idx->TriangLevel, err_idx);
//        err->SetIdx(err_idx);
//        NumMarked= EstimateError(new_x, 0.2, &Estimator);
//TODO: Fehler schaetzen
//        new_marks= EstimateError(new_x, 1.5, akt_glob_err, &ResidualErrEstimator);
//        err->Reset();
//        DeleteNumbering(err_idx);
        std::swap(v2, v1);
        std::swap(p2, p1);
        std::swap(vidx2, vidx1);
        std::swap(pidx2, pidx1);
        std::cerr << std::endl;
    }
    while (++step<maxStep);
    
    // we want the solution to be in _v
    if (v2 == &loc_v)
    {
        Stokes.v.SetIdx(&Stokes.vel_idx);
        Stokes.p.SetIdx(&Stokes.pr_idx);
        *Stokes.v.RowIdx= *loc_v.RowIdx;
        *Stokes.p.RowIdx= *loc_p.RowIdx;
        
        Stokes.v.Data.resize(loc_v.Data.size());
        Stokes.v.Data= loc_v.Data;
        Stokes.p.Data.resize(loc_p.Data.size());
        Stokes.p.Data= loc_p.Data;
    }
}

} // end of namespace DROPS


void MarkDrop (DROPS::MultiGridCL& mg, DROPS::Uint maxLevel)
{
    DROPS::Point3DCL Mitte; Mitte[0]=0.5; Mitte[1]=0.5; Mitte[2]=0.5;

    for (DROPS::MultiGridCL::TriangTetraIteratorCL It(mg.GetTriangTetraBegin(maxLevel)),
             ItEnd(mg.GetTriangTetraEnd(maxLevel)); It!=ItEnd; ++It)
    {
        if ( (GetBaryCenter(*It)-Mitte).norm()<=std::max(0.1,1.5*pow(It->GetVolume(),1.0/3.0)) )
            It->SetRegRefMark();
    }
}


void UnMarkDrop (DROPS::MultiGridCL& mg, DROPS::Uint maxLevel)
{
    DROPS::Point3DCL Mitte; Mitte[0]=0.5; Mitte[1]=0.5; Mitte[2]=0.5;

    for (DROPS::MultiGridCL::TriangTetraIteratorCL It(mg.GetTriangTetraBegin(maxLevel)),
             ItEnd(mg.GetTriangTetraEnd(maxLevel)); It!=ItEnd; ++It)
    {
        if ( (GetBaryCenter(*It)-Mitte).norm()<=std::max(0.1,1.5*pow(It->GetVolume(),1.0/3.0)) )
            It->SetRemoveMark();
    }
}

int main (int argc, char** argv)
{
  try
  {
    if (argc!=3)
    {
        std::cerr << "You have to specify two parameters:\n\tFSdrops <omega> <inner_iter_tol>" << std::endl;
        return 1;
    }

    DROPS::Point3DCL null(0.0);
    DROPS::Point3DCL e1(0.0), e2(0.0), e3(0.0);
    e1[0]= e2[1]= e3[2]= M_PI/4.;

    typedef DROPS::InstatStokesP2P1CL<DROPS::BrickBuilderCL, MyPDE::StokesCoeffCL> 
            InstatStokesOnBrickCL;
    typedef InstatStokesOnBrickCL MyStokesCL;

    DROPS::BrickBuilderCL brick(null, e1, e2, e3, 1, 1, 1);
//    DROPS::BBuilderCL brick(null, e1, e2, e3, 4, 4, 4, 2, 2, 2);
//    DROPS::LBuilderCL brick(null, e1, e2, e3, 4, 4, 4, 2, 2);
    const bool IsNeumann[6]= 
        {false, false, false, false, false, false};
    const DROPS::InstatStokesVelBndDataCL::bnd_val_fun bnd_fun[6]= 
        { &MyPDE::LsgVel, &MyPDE::LsgVel, &MyPDE::LsgVel, 
          &MyPDE::LsgVel, &MyPDE::LsgVel, &MyPDE::LsgVel};
        
    MyStokesCL prob(brick, MyPDE::Coeff, DROPS::InstatStokesBndDataCL(6, IsNeumann, bnd_fun));
    DROPS::MultiGridCL& mg = prob.GetMG();

    double omega= atof(argv[1]);
    double inner_iter_tol= atof(argv[2]);
    std::cerr << "Omega: " << omega << " inner iter tol: " << inner_iter_tol << std::endl;

    Strategy(prob, omega, inner_iter_tol, 3);

    std::cerr << DROPS::SanityMGOutCL(mg) << std::endl;

    std::ofstream fil("ttt.off");
    DROPS::RBColorMapperCL colormap;
    double min= prob.p.Data.min(),
           max= prob.p.Data.max();
           std::cerr << "pressure solution: min = " << min << ", max = " << max <<std::endl;
    fil << DROPS::GeomSolOutCL<MyStokesCL::DiscPrSolCL>(mg, prob.GetPrSolution(), &colormap, -1, false, 0.0, min, max)
        << std::endl;

    return 0;
  }
  catch (DROPS::DROPSErrCL err) { err.handle(); }
}
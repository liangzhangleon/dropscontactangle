//**************************************************************************
// File:    adaptriang.tpp                                                 *
// Content: adaptive triangulation based on position of the interface      *
//          provided by the levelset function                              *
// Author:  Sven Gross, Joerg Peters, Volker Reichelt, IGPM RWTH Aachen    *
//**************************************************************************

namespace DROPS
{

template <class DistFctT>
void AdapTriangCL::MakeInitialTriang( DistFctT& Dist)
{
    TimerCL time;

    time.Reset();
    time.Start();
    const Uint min_ref_num= f_level_ - c_level_;
    Uint i;
    for (i=0; i<2*min_ref_num; ++i)
        ModifyGridStep( Dist);
    time.Stop();
    std::cout << "MakeInitialTriang: " << i
              << " refinements in " << time.GetTime() << " seconds\n"
              << "last level: " << mg_.GetLastLevel() << '\n';
    mg_.SizeInfo( std::cout);
}

template <class DistFctT>
bool AdapTriangCL::ModifyGridStep( DistFctT& Dist)
// One step of grid change; returns true if modifications were necessary,
// false, if nothing changed.
{
    bool modified= false;
    for (MultiGridCL::TriangTetraIteratorCL it= mg_.GetTriangTetraBegin(),
         end= mg_.GetTriangTetraEnd(); it!=end; ++it)
    {
        double d= 1e99;
        int num_pos= 0;
        for (Uint j=0; j<4; ++j)
        {
            const double dist= GetValue( Dist, *it->GetVertex( j));
            if (dist>=0) ++num_pos;
            d= std::min( d, std::abs( dist));
        }
        for (Uint j=0; j<6; ++j)
        {
            const double dist= GetValue( Dist, *it->GetEdge( j));
            if (dist>=0) ++num_pos;
            d= std::min( d, std::abs( dist));
        }
        d= std::min( d, std::abs( GetValue( Dist, *it)));

        const bool vzw= num_pos!=0 && num_pos!=10; // change of sign
        const Uint l= it->GetLevel();
        // In the shell:      level should be f_level_.
        // Outside the shell: level should be c_level_.
        const Uint soll_level= (d<=width_ || vzw) ? f_level_ : c_level_;

        if (l !=  soll_level || (l == soll_level && !it->IsRegular()) )
        { // tetra will be marked for refinement/removement
            modified= true;
            if (l <= soll_level)
                it->SetRegRefMark();
            else // l > soll_level
                it->SetRemoveMark();
        }
    }
    if (modified)
        mg_.Refine();
    return modified;
}

template <class StokesT>
void AdapTriangCL::UpdateTriang (StokesT& NS, LevelsetP2CL& lset, TransportP1CL* c)
{
    TimerCL time;

    time.Reset();
    time.Start();
    VelVecDescCL  loc_v;
    VecDescCL     loc_p;
    VecDescCL     loc_l;
    VecDescCL     loc_ct;
    VelVecDescCL *v1= &NS.v,
                 *v2= &loc_v;
    VecDescCL    *p1= &NS.p,
                 *p2= &loc_p,
                 *l1= &lset.Phi,
                 *l2= &loc_l,
                 *c1= c != 0 ? &c->ct : 0,
                 *c2= &loc_ct;
    IdxDescCL  loc_vidx( 3, 3), loc_pidx( 1), loc_lidx( 1, 1), loc_cidx( 1);
    IdxDescCL  *vidx1= v1->RowIdx,
               *vidx2= &loc_vidx,
               *pidx1= p1->RowIdx,
               *pidx2= &loc_pidx,
               *lidx1= l1->RowIdx,
               *lidx2= &loc_lidx,
               *cidx1= c != 0 ? c1->RowIdx : 0,
               *cidx2= &loc_cidx;
    modified_= false;
    const Uint min_ref_num= f_level_ - c_level_;
    Uint i, LastLevel= mg_.GetLastLevel();
    match_fun match= mg_.GetBnd().GetMatchFun();

    P1XRepairCL p1xrepair( NS.UsesXFEM(), mg_, NS.p, NS.GetXidx());

    for (i=0; i<2*min_ref_num; ++i)
    {
        LevelsetP2CL::const_DiscSolCL sol= lset.GetSolution( *l1);
        if (!ModifyGridStep(sol))
            break;
        LastLevel= mg_.GetLastLevel();
        modified_= true;

        // Repair velocity
        std::swap( v2, v1);
        std::swap( vidx2, vidx1);
        NS.CreateNumberingVel( LastLevel, vidx1, match);
        if ( LastLevel != vidx2->TriangLevel)
        {
            std::cout << "LastLevel: " << LastLevel
                      << " vidx2->TriangLevel: " << vidx2->TriangLevel << std::endl;
            throw DROPSErrCL( "AdapTriangCL::UpdateTriang: Sorry, not yet implemented.");
        }
        v1->SetIdx( vidx1);
        typename StokesT::const_DiscVelSolCL funvel= NS.GetVelSolution( *v2);
        RepairAfterRefineP2( funvel, *v1);
        v2->Clear();
        NS.DeleteNumberingVel( vidx2);

        // Repair pressure
        std::swap( p2, p1);
        std::swap( pidx2, pidx1);
        NS.CreateNumberingPr( LastLevel, pidx1, match);
        p1->SetIdx( pidx1);
        typename StokesT::const_DiscPrSolCL funpr= NS.GetPrSolution( *p2);
        RepairAfterRefineP1( funpr, *p1);
        p2->Clear();
        NS.DeleteNumberingPr( pidx2);

        // Repair levelset
        std::swap( l2, l1);
        std::swap( lidx2, lidx1);
        lset.CreateNumbering( LastLevel, lidx1, match);
        l1->SetIdx( lidx1);
        LevelsetP2CL::const_DiscSolCL funlset= lset.GetSolution( *l2);
        RepairAfterRefineP2( funlset, *l1);
        l2->Clear();
        lset.DeleteNumbering( lidx2);

        // Repair concentration, if it exists.
        if (c != 0)
        {
            std::swap( c2, c1);
            std::swap( cidx2, cidx1);
            c->CreateNumbering( LastLevel, cidx1);
            c1->SetIdx( cidx1);
            typename TransportP1CL::const_DiscSolCL func= c->GetSolution( *c2);
            RepairAfterRefineP1( func, *c1);
            c2->Clear();
            c->DeleteNumbering( cidx2);
        }    
    }
    // We want the solution to be in NS.v, NS.pr, lset.Phi and c.ct
    if (v1 == &loc_v)
    {
        NS.vel_idx.swap( loc_vidx);
        NS.pr_idx.swap( loc_pidx);
        lset.idx.swap( loc_lidx);
        NS.v.SetIdx( &NS.vel_idx);
        NS.p.SetIdx( &NS.pr_idx);
        lset.Phi.SetIdx( &lset.idx);
        if(c!=0)
        {
            c->idx.swap(loc_cidx);
            c->c.SetIdx(&c->idx);
            c->ct.Data =loc_ct.Data;
        }    

        NS.v.Data= loc_v.Data;
        NS.p.Data= loc_p.Data;
        lset.Phi.Data= loc_l.Data;
    }
    p1xrepair( lset);
    time.Stop();
    std::cout << "UpdateTriang: " << i
              << " refinements/interpolations in " << time.GetTime() << " seconds\n"
              << "last level: " << LastLevel << '\n';
    mg_.SizeInfo( std::cout);
}

} // end of namespace DROPS

/// \file quadrature.cpp
/// \brief numerical integration at the interface
/// \author LNM RWTH Aachen: Joerg Grande; SC RWTH Aachen:

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
 * Copyright 2011 LNM/SC RWTH Aachen, Germany
 */

#include "num/quadrature.h"

namespace DROPS {

void
aitken_neville_zero (const CompositeQuadratureTypesNS::WeightContT& x, std::vector<CompositeQuadratureTypesNS::WeightContT>& w, Uint j_begin, Uint j_end)
{
    for (Uint j= j_begin; j < j_end; ++j)
        for (Uint i= w.size() - 1 ; i >= j; --i)
            w[i]= (x[i]*w[i - 1] - x[i - j]*w[i])/(x[i] - x[i - j]);
}

} // end of namespace DROPS

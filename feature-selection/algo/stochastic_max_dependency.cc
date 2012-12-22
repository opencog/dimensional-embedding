/** algo/stochastic_max_dependency.cc ---
 *
 * Copyright (C) 2011 OpenCog Foundation
 * Copyright (C) 2012 Poulin Holdings LLC
 *
 * Author: Linas Vepstas <linasvepstas@gmail.com
 *         Nil Geisweiller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <boost/range/irange.hpp>

#include "stochastic_max_dependency.h"

namespace opencog {
    
using namespace std;

feature_set smd_select_features(const CTable& ctable,
                                const feature_selection_parameters& fs_params)
{
    auto ir = boost::irange(0, ctable.get_arity());
    feature_set all_features(ir.begin(), ir.end()),
        init_features = initial_features(ctable.get_input_labels(), fs_params);
    if (fs_params.target_size > 0) {
        fs_scorer<set<arity_t> > fs_sc(ctable, fs_params);
        return stochastic_max_dependency_selection(all_features, init_features,
                                                   fs_sc,
                                                   (unsigned) fs_params.target_size,
                                                   fs_params.threshold,
                                                   fs_params.smd_top_size);
    } else {
        // Nothing happened, return the all features by default
        return all_features;
    }
}


} // ~namespace opencog

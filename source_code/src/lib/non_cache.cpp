/*

   Copyright (c) 2006-2010, The Scripps Research Institute

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   Author: Dr. Oleg Trott <ot14@columbia.edu>, 
           The Olson Lab, 
           The Scripps Research Institute

*/

#include "non_cache.h"
#include "curl.h"

non_cache::non_cache(const model& m, const grid_dims& gd_, const precalculate* p_, fl slope_) : sgrid(m, szv_grid_dims(gd_), p_->cutoff_sqr()), gd(gd_), p(p_), slope(slope_) {}

fl non_cache::eval      (const model& m, fl v) const { // clean up
	fl e = 0;
	const fl cutoff_sqr = p->cutoff_sqr();

	sz n = num_atom_types(p->atom_typing_used());

	VINA_FOR(i, m.num_movable_atoms()) {
		fl this_e = 0;
		fl out_of_bounds_penalty = 0;
		const atom& a = m.atoms[i];
		sz t1 = a.get(p->atom_typing_used());
		if(t1 >= n) continue;
		const vec& a_coords = m.coords[i];
		vec adjusted_a_coords; adjusted_a_coords = a_coords;
		VINA_FOR_IN(j, gd) {
			if(gd[j].n > 0) {
				if     (a_coords[j] < gd[j].begin) { adjusted_a_coords[j] = gd[j].begin; out_of_bounds_penalty += std::abs(a_coords[j] - gd[j].begin); }
				else if(a_coords[j] > gd[j].end  ) { adjusted_a_coords[j] = gd[j]  .end; out_of_bounds_penalty += std::abs(a_coords[j] - gd[j]  .end); }
			}
		}
		out_of_bounds_penalty *= slope;

		const szv& possibilities = sgrid.possibilities(adjusted_a_coords);

		VINA_FOR_IN(possibilities_j, possibilities) {
			const sz j = possibilities[possibilities_j];
			const atom& b = m.grid_atoms[j];
			sz t2 = b.get(p->atom_typing_used());
			if(t2 >= n) continue;
			vec r_ba; r_ba = adjusted_a_coords - b.coords; // FIXME why b-a and not a-b ?
			fl r2 = sqr(r_ba);
            fl theta;
            if (xs_hal_any_bond_possible(a.xs, b.xs)) {     //if there is a halogen bond possible, we need the angle
                if (xs_is_halogen(a.xs)) {                  //a is the halogen, b is the acceptor
                    const std::vector<bond>& bonds = a.bonds;
                    VINA_FOR_IN(j, bonds) {
                        const bond& bs = bonds[j];
                        const atom& c = m.get_atom(bs.connected_atom_index);
                        if(c.el == EL_TYPE_C){              //the halogen needs to be connected to a carbon
                            vec C_coords = m.coords[bs.connected_atom_index.i];     //coords of carbon
                            vec v1 = m.coords[i] - C_coords;  //vector between halogen and carbon
                            vec v2 = m.coords[i] - b.coords;  //vector between halogen and acceptor
                            theta = angle(v1, v2);
                        }
                    }
                }
                else {                                  //b is the halogen, a is the acceptor (might never happen?)
                    const std::vector<bond>& bonds = b.bonds;
                    VINA_FOR_IN(j, bonds) {
                        const bond& bs = bonds[j];
                        const atom& c = m.get_atom(bs.connected_atom_index);
                        if(c.el == EL_TYPE_C){              //the halogen needs to be connected to a carbon
                            vec v1 = b.coords - c.coords;  //vector between halogen and carbon
                            vec v2 = b.coords - m.coords[i];  //vector between halogen and acceptor
                            fl theta = angle(v1, v2);
                        }
                    }
                }
            }
            if (xs_sul_bond_possible(a.xs, b.xs)) {     //if there is a halogen bond possible, we need the angle
                if (xs_is_S(a.xs)) {                    //a is the sulfur, b is the acceptor
                    const std::vector<bond>& bonds = a.bonds;
                    if (bonds.size() == 2) {          //sulfur must be bonded to exactly two carbons
                        flv theta_holder;
                        VINA_FOR_IN(j, bonds) {
                            const bond& bs = bonds[j];
                            const atom& c = m.get_atom(bs.connected_atom_index);
                            if(c.ad == AD_TYPE_A){              //the sulfur needs to be connected to an aromatic carbon
                                vec C_coords = m.coords[bs.connected_atom_index.i];     //coords of carbon
                                vec v1 = m.coords[i] - C_coords;  //vector between sulfur and carbon
                                vec v2 = m.coords[i] - b.coords;  //vector between sulfur and acceptor
                                theta_holder.push_back(angle(v1, v2));
                            }
                            else {
                                theta_holder.push_back(0);         //if the sulfur is bonded to a non-aromatic carbon, the angle is 0
                            }
                        }
                        theta = theta_holder[0] > theta_holder[1] ? theta_holder[0] : theta_holder[1]; //only care about the bigger angle
                    }
                    else {                  //if the sulfur doesn't have exactly 2 bonds, set theat to 0
                        theta = 0;
                    }
                }
            }
            else {
                theta = 180;
            }
			if(r2 < cutoff_sqr) {
				sz type_pair_index = get_type_pair_index(p->atom_typing_used(), a, b);
                this_e +=  p->eval_fast(type_pair_index, r2, theta);
			}
		}
		curl(this_e, v);
		e += this_e + out_of_bounds_penalty;
	}
	return e;
}

bool non_cache::within(const model& m, fl margin) const {
	VINA_FOR(i, m.num_movable_atoms()) {
		if(m.atoms[i].is_hydrogen()) continue;
		const vec& a_coords = m.coords[i];
		VINA_FOR_IN(j, gd)
			if(gd[j].n > 0)
				if(a_coords[j] < gd[j].begin - margin || a_coords[j] > gd[j].end + margin) 
					return false;
	}
	return true;
}

fl non_cache::eval_deriv(      model& m, fl v) const { // clean up
	fl e = 0;
	const fl cutoff_sqr = p->cutoff_sqr();

	sz n = num_atom_types(p->atom_typing_used());

	VINA_FOR(i, m.num_movable_atoms()) {
		fl this_e = 0;
		vec deriv(0, 0, 0);
		vec out_of_bounds_deriv(0, 0, 0);
		fl out_of_bounds_penalty = 0;
		const atom& a = m.atoms[i];
		sz t1 = a.get(p->atom_typing_used());
		if(t1 >= n) { m.minus_forces[i].assign(0); continue; }
		const vec& a_coords = m.coords[i];
		vec adjusted_a_coords; adjusted_a_coords = a_coords;
		VINA_FOR_IN(j, gd) {
			if(gd[j].n > 0) {
				if     (a_coords[j] < gd[j].begin) { adjusted_a_coords[j] = gd[j].begin; out_of_bounds_deriv[j] = -1; out_of_bounds_penalty += std::abs(a_coords[j] - gd[j].begin); }
				else if(a_coords[j] > gd[j].end  ) { adjusted_a_coords[j] = gd[j]  .end; out_of_bounds_deriv[j] =  1; out_of_bounds_penalty += std::abs(a_coords[j] - gd[j]  .end); }
			}
		}
		out_of_bounds_penalty *= slope;
		out_of_bounds_deriv *= slope;

		const szv& possibilities = sgrid.possibilities(adjusted_a_coords);

		VINA_FOR_IN(possibilities_j, possibilities) {
			const sz j = possibilities[possibilities_j];
			const atom& b = m.grid_atoms[j];
			sz t2 = b.get(p->atom_typing_used());
			if(t2 >= n) continue;
			vec r_ba; r_ba = adjusted_a_coords - b.coords; // FIXME why b-a and not a-b ?
			fl r2 = sqr(r_ba);
            fl theta;
            if (xs_hal_any_bond_possible(a.xs, b.xs)) {     //if there is a halogen bond possible, we need the angle
                if (xs_is_halogen(a.xs)) {                  //a is the halogen, b is the acceptor
                    const std::vector<bond>& bonds = a.bonds;
                    VINA_FOR_IN(j, bonds) {
                        const bond& bs = bonds[j];
                        const atom& c = m.get_atom(bs.connected_atom_index);
                        if(c.el == EL_TYPE_C){              //the halogen needs to be connected to a carbon
                            vec C_coords = m.coords[bs.connected_atom_index.i];     //coords of carbon
                            vec v1 = m.coords[i] - C_coords;  //vector between halogen and carbon
                            vec v2 = m.coords[i] - b.coords;  //vector between halogen and acceptor
                            theta = angle(v1, v2);
                        }
                    }
                }
                else {                                  //b is the halogen, a is the acceptor (might never happen?)
                    const std::vector<bond>& bonds = b.bonds;
                    VINA_FOR_IN(j, bonds) {
                        const bond& bs = bonds[j];
                        const atom& c = m.get_atom(bs.connected_atom_index);
                        if(c.el == EL_TYPE_C){              //the halogen needs to be connected to a carbon
                            vec v1 = b.coords - c.coords;  //vector between halogen and carbon
                            vec v2 = b.coords - m.coords[i];  //vector between halogen and acceptor
                            fl theta = angle(v1, v2);
                        }
                    }
                }
            }
            if (xs_sul_bond_possible(a.xs, b.xs)) {     //if there is a halogen bond possible, we need the angle
                if (xs_is_S(a.xs)) {                    //a is the sulfur, b is the acceptor
                    const std::vector<bond>& bonds = a.bonds;
                    if (bonds.size() == 2) {          //sulfur must be bonded to exactly two carbons
                        flv theta_holder;
                        VINA_FOR_IN(j, bonds) {
                            const bond& bs = bonds[j];
                            const atom& c = m.get_atom(bs.connected_atom_index);
                            if(c.ad == AD_TYPE_A){              //the sulfur needs to be connected to an aromatic carbon
                                vec C_coords = m.coords[bs.connected_atom_index.i];     //coords of carbon
                                vec v1 = m.coords[i] - C_coords;  //vector between sulfur and carbon
                                vec v2 = m.coords[i] - b.coords;  //vector between sulfur and acceptor
                                theta_holder.push_back(angle(v1, v2));
                            }
                            else {
                                theta_holder.push_back(0);         //if the sulfur is bonded to a non-aromatic carbon, the angle is 0
                            }
                        }
                        theta = theta_holder[0] > theta_holder[1] ? theta_holder[0] : theta_holder[1]; //only care about the bigger angle
                    }
                    else {                  //if the sulfur doesn't have exactly 2 bonds, set theat to 0
                        theta = 0;
                    }
                }
            }
            else {
                theta = 180;
            }
			if(r2 < cutoff_sqr) {
				sz type_pair_index = get_type_pair_index(p->atom_typing_used(), a, b);
                pr e_dor =  p->eval_deriv(type_pair_index, r2, theta);
				this_e += e_dor.first;
				deriv += e_dor.second * r_ba;
			}
		}
		curl(this_e, deriv, v);
		m.minus_forces[i] = deriv + out_of_bounds_deriv;
		e += this_e + out_of_bounds_penalty;
	}
	return e;
}

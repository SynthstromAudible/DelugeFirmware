/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "effects/sidechain.h"

#include "dsp-next/core/fixed_point.h"
#include "plugin-api/types/sync.h"

namespace deluge::effects {

dsp::fixed::DuckEnvelope::Settings Sidechain::settings(const SidechainState& state, uint32_t time_per_tick_inverse,
                                                       dsp::q31 shape) {
	dsp::fixed::DuckEnvelope::Settings out{
	    .attack = state.attack,
	    .release = state.release,
	    .shape = shape,
	    .instant_attack = state.attack == kInstantAttack,
	};
	if (state.sync_level == plugin::SyncLevel::NONE) {
		return out;
	}

	// The tick inverse is the multiply's signed Q31 operand. A step word lifted to Q11 (the attack) or Q13 (the
	// release) times it is Q10 or Q12, and that word, shifted by the level (a run-time shift, so raw), is the duck's
	// step: the step word times the inverse, times 2^10 or 2^12.
	const auto inverse = dsp::Q<31>::from_raw(static_cast<int32_t>(time_per_tick_inverse));
	const int32_t level = plugin::index_of(state.sync_level);

	const int32_t attack_shift = (9 - level) - 2;
	out.attack = mul(as_wrap<dsp::Q<11>>(dsp::Q<0>::from_raw(state.attack)), inverse).raw();
	if (attack_shift >= 0) {
		out.attack >>= attack_shift;
	}
	else {
		out.attack <<= -attack_shift;
	}

	out.release = mul(as_wrap<dsp::Q<13>>(dsp::Q<0>::from_raw(state.release)), inverse).raw() >> (9 - level);
	return out;
}

void Sidechain::registerHitRetrospectively(int32_t strength, uint32_t samples_ago, const SidechainState& state,
                                           uint32_t time_per_tick_inverse) {
	// hit_at reads no shape, so the middle one serves.
	duck_.configure(settings(state, time_per_tick_inverse, 0));
	duck_.on<&dsp::fixed::DuckEnvelope::hit_at>(strength, samples_ago);
}

int32_t Sidechain::render(uint32_t num_samples, int32_t shape, const SidechainState& state,
                          uint32_t time_per_tick_inverse) {
	duck_.configure(settings(state, time_per_tick_inverse, shape));
	return duck_.advance(num_samples);
}

} // namespace deluge::effects

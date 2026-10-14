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

#include "engines/effects/filter.h"

#include "engines/effects/filter_slots.h"

#include <cstddef>
#include <limits>
#include <type_traits>

namespace deluge::effects {

int32_t Filter::configure(const FilterState& state, const FilterParams& params, int32_t post_fx_volume) {
	constexpr int32_t kMinimum = std::numeric_limits<int32_t>::min();
	const bool low_on = state.lpf_mode == FilterMode::TRANSISTOR_24DB_DRIVE || params.lpf_frequency_param < kLowPassOpen
	                    || params.lpf_morph_param > kMinimum;
	const bool high_on = params.hpf_frequency_param > kMinimum || params.hpf_morph_param > kMinimum;
	return set_.configure(post_fx_volume,
	                      {
	                          .frequency = params.lpf_frequency,
	                          .resonance = params.lpf_resonance,
	                          .morph = params.lpf_morph,
	                          .allow_oversampling = params.allow_oversampling,
	                      },
	                      low_on ? low_pass_slot(state.lpf_mode) : dsp::fixed::LowPassSlot{},
	                      {
	                          .frequency = params.hpf_frequency,
	                          .resonance = params.hpf_resonance,
	                          .morph = params.hpf_morph,
	                          .allow_oversampling = params.allow_oversampling,
	                      },
	                      high_on ? high_pass_slot(state.hpf_mode) : dsp::fixed::HighPassSlot{},
	                      filter_route(state.route));
}

void Filter::process(std::span<dsp::StereoSample> block, dsp::fixed::FilterScratch scratch) {
	// The set reads the frames as one interleaved run of samples, left then right.
	static_assert(std::is_standard_layout_v<dsp::StereoSample>);
	static_assert(offsetof(dsp::StereoSample, l) == 0);
	static_assert(offsetof(dsp::StereoSample, r) == sizeof(dsp::q31));
	static_assert(sizeof(dsp::StereoSample) == 2 * sizeof(dsp::q31));
	set_.renderStereo({&block.data()->l, block.size() * 2}, scratch);
}

} // namespace deluge::effects

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

#include "effects/filter.h"

#include "dsp-next/core/buffer_view.h"
#include "effects/filter_slots.h"

#include <cstddef>
#include <limits>
#include <type_traits>

namespace deluge::effects {

FilterParams Filter::params_from(const Words& words, bool allow_oversampling) {
	return {
	    .lpf_frequency = final_value(kParams[0], words.lpf_frequency),
	    .lpf_resonance = final_value(kParams[1], words.lpf_resonance),
	    .lpf_morph = final_value(kParams[2], words.lpf_morph),
	    .hpf_frequency = final_value(kParams[3], words.hpf_frequency),
	    .hpf_resonance = final_value(kParams[4], words.hpf_resonance),
	    .hpf_morph = final_value(kParams[5], words.hpf_morph),
	    .lpf_frequency_param = words.lpf_frequency,
	    .lpf_morph_param = words.lpf_morph,
	    .hpf_frequency_param = words.hpf_frequency,
	    .hpf_morph_param = words.hpf_morph,
	    .allow_oversampling = allow_oversampling,
	};
}

dsp::Q<27> Filter::configure(const FilterState& state, const FilterParams& params, dsp::Q<27> post_fx_volume) {
	constexpr int32_t kMinimum = std::numeric_limits<int32_t>::min();
	const bool low_on = state.lpf_mode == FilterMode::TRANSISTOR_24DB_DRIVE || params.lpf_frequency_param < kLowPassOpen
	                    || params.lpf_morph_param > kMinimum;
	const bool high_on = params.hpf_frequency_param > kMinimum || params.hpf_morph_param > kMinimum;
	// FilterParams holds final_value's words; each field's format is its doc's. The set's gain is a word in any
	// format, so the volume's Q27 word goes through it.
	return dsp::Q<27>::from_raw(set_.configure(post_fx_volume.raw(),
	                                           {
	                                               .frequency = dsp::Q<31>::from_raw(params.lpf_frequency),
	                                               .resonance = dsp::Q<29>::from_raw(params.lpf_resonance),
	                                               .morph = dsp::Q<29>::from_raw(params.lpf_morph),
	                                               .allow_oversampling = params.allow_oversampling,
	                                           },
	                                           low_on ? low_pass_slot(state.lpf_mode) : dsp::fixed::LowPassSlot{},
	                                           {
	                                               .frequency = dsp::Q<31>::from_raw(params.hpf_frequency),
	                                               .resonance = dsp::Q<29>::from_raw(params.hpf_resonance),
	                                               .morph = dsp::Q<29>::from_raw(params.hpf_morph),
	                                               .allow_oversampling = params.allow_oversampling,
	                                           },
	                                           high_on ? high_pass_slot(state.hpf_mode) : dsp::fixed::HighPassSlot{},
	                                           filter_route(state.route)));
}

void Filter::process(std::span<dsp::StereoSample> block, dsp::fixed::FilterScratch scratch, dsp::fixed::Cong& noise) {
	// The set reads the frames as one interleaved run of samples, left then right.
	static_assert(std::is_standard_layout_v<dsp::StereoSample>);
	static_assert(offsetof(dsp::StereoSample, l) == 0);
	static_assert(offsetof(dsp::StereoSample, r) == sizeof(int32_t));
	static_assert(sizeof(dsp::StereoSample) == 2 * sizeof(int32_t));
	set_.render_stereo(std::span<dsp::Q<31>>{&block.data()->l, block.size() * 2}, scratch, noise);
}

} // namespace deluge::effects

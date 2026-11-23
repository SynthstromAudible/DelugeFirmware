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

#pragma once

#include "definitions_cxx.hpp" // kMaxNumVoicesUnison
#include "dsp/core/fixed_point.h"
#include "dsp/fixed/pitch/fine_tune.h"
#include "modulation/params/param.h"
#include "synth/matrix/param_format.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

/// @file
/// @brief How a sound's unison parts sit in pitch and across the field.

namespace deluge::synth {

/// @brief Unison as the voice plays it: each part's detune and pan, and the level that keeps the parts' sum at one
/// voice's.
/// @details With one part (or none) the detunes and pans are not read: the engines play the single part undetuned
/// and unpanned.
struct UnisonLayout {
	uint8_t count{1};
	std::array<dsp::fixed::FineTune, kMaxNumVoicesUnison> detunes{};
	std::array<dsp::Q<31>, kMaxNumVoicesUnison> pans{};
	dsp::Q<31> level{}; ///< the volume param's neutral over √count, the level each part is scaled to
	bool operator==(const UnisonLayout&) const = default;
};

/// @brief The layout of @p count parts spread over @p detune cents and @p spread of the stereo field.
/// @param count Parts per voice.
/// @param detune The cents between the lowest and highest part.
/// @param spread How far the parts spread across the field, 0..50.
[[nodiscard]] inline UnisonLayout derive_unison(uint8_t count, int8_t detune, uint8_t spread) noexcept {
	UnisonLayout layout{.count = count};
	// The volume's neutral over the square root of the unison count, in float
	constexpr float kNeutral = dsp::to_float(matrix::param_neutral<modulation::params::LOCAL_VOLUME>());
	layout.level = dsp::from_float<31>(kNeutral / sqrtf(count));
	if (count == 1) {
		return layout;
	}
	const int32_t last = count - 1;
	// The detunes run from half the detune below to half above, evenly; an odd count's middle part is undetuned.
	const int32_t detune_span = int32_t{detune} * dsp::fixed::FineTune::kCentStep;
	const int32_t lowest_detune = -(detune_span >> 1);
	const int32_t detune_spacing = detune_span / last;
	// The pans run across the spread likewise, the parts alternating sides (-2 +1 0 -1 +2) so detune and pan mix.
	const int32_t spread_span = int32_t{spread} * dsp::fixed::FineTune::kCentStep;
	const int32_t lowest_pan = -(spread_span >> 1);
	const int32_t pan_spacing = spread_span / last;
	for (int32_t u = 0; u < count; u++) {
		const bool middle = (count & 1) != 0 && u == (last >> 1);
		layout.detunes[u] =
		    middle ? dsp::fixed::FineTune{} : dsp::fixed::FineTune::of_step(lowest_detune + detune_spacing * u);
		const int32_t sign = (std::min<int32_t>(u, last - u) & 1) != 0 ? -1 : 1;
		// Crossing: the pans are whole steps of the spread across the field
		layout.pans[u] = dsp::Q<31>::from_raw(sign * (lowest_pan + pan_spacing * u));
	}
	return layout;
}

} // namespace deluge::synth

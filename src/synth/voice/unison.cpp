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

#include "synth/voice/unison.h"

#include "modulation/params/param.h"
#include "synth/matrix/param_format.h"

#include <algorithm>
#include <cmath>

namespace deluge::synth {

UnisonLayout derive_unison(uint8_t count, int8_t detune, uint8_t spread) noexcept {
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

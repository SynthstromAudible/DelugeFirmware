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

#include "dsp-next/core/stereo_sample.h"

#include <cstdint>
#include <span>

namespace deluge::effects {

/// @brief One block's EQ settings: the four params' Q31 values (a band at 0 is flat).
struct EqualizerParams {
	int32_t bass{0};
	int32_t treble{0};
	int32_t bass_frequency{0};
	int32_t treble_frequency{0};
};

/// @brief Two shelving bands, each a one-pole split of the signal; a flat band neither processes nor moves its memory.
class Equalizer {
public:
	void process(std::span<dsp::StereoSample> block, const EqualizerParams& params);

	/// @brief Takes @p other's band coefficients, as a clone of its owner does; the filter memory starts afresh.
	void copy_coefficients_from(const Equalizer& other) {
		bass_coefficient_ = other.bass_coefficient_;
		treble_coefficient_ = other.treble_coefficient_;
	}

	void reset() {
		without_treble_ = {};
		bass_only_ = {};
	}

private:
	int32_t bass_coefficient_{0};   ///< Q31, from exp_of(120000000, …), recomputed while the bass band is on
	int32_t treble_coefficient_{0}; ///< Q31, from exp_of(700000000, …), recomputed while the treble band is on
	dsp::StereoSample without_treble_{};
	dsp::StereoSample bass_only_{};
};

} // namespace deluge::effects

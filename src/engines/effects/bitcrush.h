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
#include <limits>
#include <span>

namespace deluge::effects {

/// @brief One block's bitcrush settings: the two params' Q31 values. The bitcrush is on from -2113929216; the
/// sample-rate reduction is on above -2^31.
struct BitcrushParams {
	int32_t bitcrush{std::numeric_limits<int32_t>::min()};
	int32_t sample_rate_reduction{std::numeric_limits<int32_t>::min()};
};

/// @brief Bit depth reduction and sample-rate reduction, with the down- and up-conversion memory the latter carries.
class Bitcrush {
public:
	/// @brief Crushes @p block in place; a strong bitcrush lowers @p post_fx_volume.
	/// @param block          The audio to crush.
	/// @param params         The block's bitcrush and sample-rate-reduction values.
	/// @param post_fx_volume The post-FX volume, Q31; lowered for a strong bitcrush.
	void process(std::span<dsp::StereoSample> block, const BitcrushParams& params, int32_t& post_fx_volume);

private:
	bool srr_on_last_time_{false};
	uint32_t low_rate_pos_{0};  ///< 22-bit fixed point: 4194304 is one sample
	uint32_t high_rate_pos_{0}; ///< 22-bit fixed point
	dsp::StereoSample last_sample_{};
	dsp::StereoSample grabbed_{};
	dsp::StereoSample last_grabbed_{};
};

} // namespace deluge::effects

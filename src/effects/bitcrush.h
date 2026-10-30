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

#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/shaping/bit_depth.h"
#include "dsp-next/fixed/shaping/sample_rate_reducer.h"

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

/// @brief What a crushed block asks of its owner: how far to shift its post-FX volume down.
struct BitcrushBlock {
	int volume_shift = 0; ///< 0 to 3: a bitcrush above preset 4 lowers the volume.
};

/// @brief Bit-depth reduction and sample-rate reduction: the depth's mask alone while the rate is not reduced; the
/// reducer, masking its grabs one bit less, while it is.
class Bitcrush {
public:
	/// @brief Crushes @p block in place.
	/// @param block  The audio to crush.
	/// @param params The block's bitcrush and sample-rate-reduction values.
	/// @return The shift a strong bitcrush asks of the owner's post-FX volume.
	[[nodiscard]] BitcrushBlock process(std::span<dsp::StereoSample> block, const BitcrushParams& params);

private:
	dsp::Mono<dsp::fixed::BitDepth> depth_;            ///< The mask, stateless.
	dsp::Mono<dsp::fixed::SampleRateReducer> reducer_; ///< The down- and up-conversion's positions and frames.
	bool reducing_{false}; ///< Whether the last block reduced the rate: switching on sends the reducer `start`.
};

} // namespace deluge::effects

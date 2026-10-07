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

#include "engines/effects/bitcrush.h"

#include "engines/effects/kernels.h"
#include "util/fixedpoint.h"

#include <algorithm>

namespace deluge::effects {

void Bitcrush::process(std::span<dsp::StereoSample> block, const BitcrushParams& params, int32_t& post_fx_volume) {
	uint32_t bitCrushMaskForSRR = 0xFFFFFFFF;

	const bool srrEnabled = params.sample_rate_reduction != std::numeric_limits<int32_t>::min();

	// Bitcrushing ------------------------------------------------------------------------------
	if (params.bitcrush >= -2113929216) {
		uint32_t positivePreset = (params.bitcrush + 2147483648) >> 29;
		if (positivePreset > 4) {
			post_fx_volume >>= (positivePreset - 4);
		}

		// If not also doing SRR
		if (!srrEnabled) {
			uint32_t mask = 0xFFFFFFFF << (19 + (positivePreset));
			for (dsp::StereoSample& sample : block) {
				sample.l &= mask;
				sample.r &= mask;
			}
		}

		else {
			bitCrushMaskForSRR = 0xFFFFFFFF << (18 + (positivePreset));
		}
	}

	// Sample rate reduction -------------------------------------------------------------------------------------
	if (srrEnabled) {

		// Set up for first time
		if (!srr_on_last_time_) {
			srr_on_last_time_ = true;
			last_sample_.l = last_sample_.r = 0;
			grabbed_.l = grabbed_.r = 0;
			low_rate_pos_ = 0;
		}

		// This function, slightly unusually, uses 22 bits to represent "1". That's 4194304. I tried using 24, but stuff
		// started clipping off where I needed it if sample rate too low

		uint32_t positivePreset = params.sample_rate_reduction + 2147483648;
		int32_t lowSampleRateIncrement = exp_of(4194304, (positivePreset >> 3));
		int32_t highSampleRateIncrement = ((uint32_t)0xFFFFFFFF / (lowSampleRateIncrement >> 6)) << 6;
		// int32_t highSampleRateIncrement = exp_of(4194304, -(int32_t)(positivePreset >> 3)); // This would work too

		for (dsp::StereoSample& sample : block) {
			// Convert down.
			// If time to "grab" another sample for down-conversion...
			if (low_rate_pos_ < 4194304) {
				int32_t strength2 = low_rate_pos_;
				int32_t strength1 = 4194303 - strength2;

				last_grabbed_ = grabbed_; // What was current is now last
				grabbed_.l = multiply_32x32_rshift32_rounded(last_sample_.l, strength1 << 9)
				             + multiply_32x32_rshift32_rounded(sample.l, strength2 << 9);
				grabbed_.r = multiply_32x32_rshift32_rounded(last_sample_.r, strength1 << 9)
				             + multiply_32x32_rshift32_rounded(sample.r, strength2 << 9);
				grabbed_.l &= bitCrushMaskForSRR;
				grabbed_.r &= bitCrushMaskForSRR;

				// Set the "time" at which we want to "grab" our next sample for down-conversion.
				low_rate_pos_ += lowSampleRateIncrement;

				// "Re-sync" the up-conversion spinner.
				// I previously had it using strength2 instead of "low_rate_pos_ & 16777215", but that just works
				// better. Ah, writing the massive explanation would take ages.
				high_rate_pos_ = multiply_32x32_rshift32_rounded(low_rate_pos_ & 4194303, highSampleRateIncrement << 8)
				                 << 2;
			}
			low_rate_pos_ -= 4194304; // We're one step closer to grabbing our next sample for down-conversion
			last_sample_ = sample;

			// Convert up
			// Would only overshoot if we raised the sample rate during playback
			int32_t strength2 = std::min(high_rate_pos_, (uint32_t)4194303);
			int32_t strength1 = 4194303 - strength2;
			sample.l = (multiply_32x32_rshift32_rounded(last_grabbed_.l, strength1 << 9)
			            + multiply_32x32_rshift32_rounded(grabbed_.l, strength2 << 9))
			           << 2;
			sample.r = (multiply_32x32_rshift32_rounded(last_grabbed_.r, strength1 << 9)
			            + multiply_32x32_rshift32_rounded(grabbed_.r, strength2 << 9))
			           << 2;

			high_rate_pos_ += highSampleRateIncrement;
		}
	}
	else {
		srr_on_last_time_ = false;
	}
}

} // namespace deluge::effects

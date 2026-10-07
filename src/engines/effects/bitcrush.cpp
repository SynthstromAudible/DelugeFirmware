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

#include "dsp-next/fixed/curves/exp_curve.h"
#include "util/fixedpoint.h"

#include <algorithm>

namespace deluge::effects {

namespace {

/// One sample in the sample-rate reduction's 22-bit fixed point (4194304 is 1).
constexpr uint32_t kOne = 4194304;
/// The largest 22-bit position below one sample.
constexpr uint32_t kBelowOne = kOne - 1;

/// @p value with @p mask applied to its bits.
[[nodiscard]] int32_t masked(int32_t value, uint32_t mask) {
	return static_cast<int32_t>(static_cast<uint32_t>(value) & mask);
}

} // namespace

void Bitcrush::process(std::span<dsp::StereoSample> block, const BitcrushParams& params, int32_t& post_fx_volume) {
	uint32_t mask_for_srr = 0xFFFFFFFFu;
	const bool srr_enabled = params.sample_rate_reduction != std::numeric_limits<int32_t>::min();

	// Bit depth reduction: the preset (0–7) masks off more low bits as it rises and, above 4, lowers the volume.
	if (params.bitcrush >= -2113929216) {
		const auto preset = static_cast<uint32_t>((static_cast<int64_t>(params.bitcrush) + 2147483648LL) >> 29);
		if (preset > 4) {
			post_fx_volume >>= (preset - 4);
		}
		if (srr_enabled) {
			// The sample-rate reduction masks the samples it grabs instead, one bit less.
			mask_for_srr = 0xFFFFFFFFu << (18 + preset);
		}
		else {
			const uint32_t mask = 0xFFFFFFFFu << (19 + preset);
			for (dsp::StereoSample& sample : block) {
				sample.l = masked(sample.l, mask);
				sample.r = masked(sample.r, mask);
			}
		}
	}

	if (!srr_enabled) {
		srr_on_last_time_ = false;
		return;
	}

	// Sample-rate reduction: grab a sample at the lower rate (interpolated between the last input and this one), then
	// interpolate back up between the last two grabbed samples. Positions are in 22-bit fixed point, which keeps the
	// low rates from clipping.
	if (!srr_on_last_time_) {
		srr_on_last_time_ = true;
		last_sample_.l = last_sample_.r = 0;
		grabbed_.l = grabbed_.r = 0;
		low_rate_pos_ = 0;
	}

	const uint32_t preset = static_cast<uint32_t>(params.sample_rate_reduction) + 2147483648u;
	const int32_t low_rate_increment =
	    dsp::fixed::exp_curve(static_cast<int32_t>(kOne), static_cast<int32_t>(preset >> 3));
	const auto high_rate_increment =
	    static_cast<int32_t>((0xFFFFFFFFu / static_cast<uint32_t>(low_rate_increment >> 6)) << 6);

	for (dsp::StereoSample& sample : block) {
		// Down: time to grab the next low-rate sample.
		if (low_rate_pos_ < kOne) {
			const auto toward_this = static_cast<int32_t>(low_rate_pos_);
			const int32_t toward_last = static_cast<int32_t>(kBelowOne) - toward_this;

			last_grabbed_ = grabbed_;
			grabbed_.l = multiply_32x32_rshift32_rounded(last_sample_.l, toward_last << 9)
			             + multiply_32x32_rshift32_rounded(sample.l, toward_this << 9);
			grabbed_.r = multiply_32x32_rshift32_rounded(last_sample_.r, toward_last << 9)
			             + multiply_32x32_rshift32_rounded(sample.r, toward_this << 9);
			grabbed_.l = masked(grabbed_.l, mask_for_srr);
			grabbed_.r = masked(grabbed_.r, mask_for_srr);

			low_rate_pos_ += static_cast<uint32_t>(low_rate_increment);

			// Re-sync the up-conversion to where the low-rate grab fell within this sample.
			high_rate_pos_ =
			    static_cast<uint32_t>(multiply_32x32_rshift32_rounded(static_cast<int32_t>(low_rate_pos_ & kBelowOne),
			                                                          high_rate_increment << 8)
			                          << 2);
		}
		low_rate_pos_ -= kOne;
		last_sample_ = sample;

		// Up: between the last two grabbed samples. The position only passes one sample if the rate rose mid-grab.
		const auto toward_grabbed = static_cast<int32_t>(std::min(high_rate_pos_, kBelowOne));
		const int32_t toward_last_grabbed = static_cast<int32_t>(kBelowOne) - toward_grabbed;
		sample.l = (multiply_32x32_rshift32_rounded(last_grabbed_.l, toward_last_grabbed << 9)
		            + multiply_32x32_rshift32_rounded(grabbed_.l, toward_grabbed << 9))
		           << 2;
		sample.r = (multiply_32x32_rshift32_rounded(last_grabbed_.r, toward_last_grabbed << 9)
		            + multiply_32x32_rshift32_rounded(grabbed_.r, toward_grabbed << 9))
		           << 2;

		high_rate_pos_ += static_cast<uint32_t>(high_rate_increment);
	}
}

} // namespace deluge::effects

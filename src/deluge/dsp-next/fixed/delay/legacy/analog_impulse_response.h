/*
 * Copyright © 2015-2026 Synthstrom Audible Limited
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
#include "util/fixedpoint.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed::legacy {

/// @brief The analog delay's 26-tap impulse response (Q31 taps), one stereo sample at a time.
///
/// A direct-form FIR kept as running partial sums: each input adds its product with every tap into the sums still
/// waiting to come out, so the output for a sample is the oldest sum plus the input times the first tap. Each product
/// is the rounded high word of the 32x32 multiply, so a Q31 input comes out Q31 scaled by the tap. The kernel form,
/// fixed::AnalogImpulseResponse, steps one channel; this class steps a stereo sample.
class AnalogImpulseResponse {
public:
	/// @param input A stereo sample, Q31.
	/// @return The response's output for it, Q31.
	[[nodiscard]] StereoSample process(StereoSample input) {
		const StereoSample output{.l = pending_[0].l + tap(input.l, 0), .r = pending_[0].r + tap(input.r, 0)};
		for (size_t i = 1; i < kPending; i++) {
			pending_[i - 1] = {.l = pending_[i].l + tap(input.l, i), .r = pending_[i].r + tap(input.r, i)};
		}
		pending_[kPending - 1] = {.l = tap(input.l, kPending), .r = tap(input.r, kPending)};
		return output;
	}

private:
	static constexpr size_t kTaps = 26;
	static constexpr size_t kPending = kTaps - 1;

	/// The taps, Q31: a short pre-ring, the main lobe at taps 8 and 9, then a damped tail.
	static constexpr std::array<int32_t, kTaps> kResponse{
	    -3203916,   8857848,   24813136,  41537808, 35217472,  15195632,  -27538592, -61984128, 1944654848,
	    1813580928, 438462784, 101125088, 6042048,  -22429488, -46218864, -56638560, -64785312, -52108528,
	    -37256992,  -11863856, 1390352,   14663296, 12784464,  14254800,  5690912,   4490736,
	};

	/// @return @p sample (Q31) times tap @p index, the rounded high word.
	[[nodiscard]] static int32_t tap(int32_t sample, size_t index) {
		return multiply_32x32_rshift32_rounded(sample, kResponse[index]);
	}

	/// The sums still to come out: entry i is the output i + 1 samples from now, less the inputs yet to arrive, Q31.
	std::array<StereoSample, kPending> pending_{};
};

} // namespace deluge::dsp::fixed::legacy

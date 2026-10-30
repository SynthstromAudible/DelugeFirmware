/*
 * Copyright © 2020-2026 Synthstrom Audible Limited
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
#include "dsp-next/fixed/interpolation/interpolator.h"
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/interpolation/sinc_interpolation.h"   // SincWindow, blend_sinc_kernel, windowed_sinc_dot
#include "dsp-next/fixed/interpolation/windowed_sinc_kernel.h" // kWindowedSincKernel
#include <argon.hpp>
#include <cstdint>
#include <limits>

namespace deluge::dsp::fixed {

namespace {
/// Load one tap window (a @ref Interpolator channel buffer) into the int16 argon vectors @ref windowed_sinc_dot wants.
[[gnu::always_inline]] inline SincWindow load_window(const int16_t* buffer) {
	SincWindow window;
	for (size_t i = 0; i < kSincWindowVectors; ++i) {
		window[i] = Argon<int16_t>::Load(&buffer[i * Argon<int16_t>::lanes]);
	}
	return window;
}
} // namespace

StereoSample Interpolator::interpolate(std::size_t channels, int32_t which_kernel, uint32_t osc_pos) {
	constexpr size_t kNumBitsInTableSize = 8;
	constexpr size_t kRshiftAmount = ((24 + kInterpolationMaxNumSamplesMagnitude) - 16 - kNumBitsInTableSize + 1);

	uint32_t rshifted;
	if constexpr (kRshiftAmount >= 0) {
		rshifted = osc_pos >> kRshiftAmount;
	}
	else {
		rshifted = osc_pos << (-kRshiftAmount);
	}

	int16_t strength2 = rshifted & std::numeric_limits<int16_t>::max();
	const auto progress_small =
	    static_cast<int32_t>(osc_pos >> (24 + kInterpolationMaxNumSamplesMagnitude - kNumBitsInTableSize));

	// Position the (shared) kernel to sub-sample accuracy once, then weight each channel's window with it.
	const int16_t* kernel_row = kWindowedSincKernel.variant(which_kernel) + progress_small * kSincKernelTaps;
	SincWindow kernel = blend_sinc_kernel(kernel_row, strength2);

	StereoSample output;
	output.l = windowed_sinc_dot(kernel, load_window(buffer_l.data()));
	if (channels == 2) {
		output.r = windowed_sinc_dot(kernel, load_window(buffer_r.data()));
	}
	return output;
}

StereoSample Interpolator::interpolate_linear(std::size_t channels, uint32_t phase) {
	StereoSample output;
	int16_t strength2 = phase >> 9;
	int16_t strength1 = std::numeric_limits<int16_t>::max() - strength2; // inverse

	output.l = (buffer_l[1] * strength1) + (buffer_l[0] * strength2);
	if (channels == 2) {
		output.r = (buffer_r[1] * strength1) + (buffer_r[0] * strength2);
	}
	return output;
}

} // namespace deluge::dsp::fixed

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
#pragma once

#include "definitions_cxx.hpp"           // kInterpolationMaxNumSamples
#include "dsp-next/core/stereo_sample.h" // deluge::dsp::StereoSample
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief A stereo windowed-sinc / linear sample interpolator over a sliding tap window (q31 backend).
///
/// Holds the most recent @ref kInterpolationMaxNumSamples input samples per channel — the *tap window*, newest at
/// index 0, oldest at the end. The owner feeds the window (@ref pushL / @ref pushR advance it by one sample,
/// @ref jumpForward by several) and reads a sub-sample-accurate output frame with @ref interpolate (windowed-sinc,
/// using the @p whichKernel-selected oversampling variant of @ref windowed_sinc_kernel) or, on the cheap path,
/// @ref interpolateLinear (2-tap linear).
///
/// The tap windows are intentionally public: the sample-playback reader fills and shifts them directly for loop
/// crossfades and jump-forward zero-fills — sample-specific window management that isn't generic interpolation, and
/// which a later refactor may lift behind a richer API. The feed/read methods are the common path.
struct [[gnu::hot]] Interpolator {
	using Window = std::array<int16_t, kInterpolationMaxNumSamples>;

	/// @brief Sub-sample-accurate windowed-sinc interpolation of the current window.
	/// @param channels 1 = mono (left window only), 2 = stereo (both).
	/// @param whichKernel the oversampling-kernel variant (from `getWhichKernel`, by playback rate).
	/// @param oscPos the sub-sample phase within the window.
	/// @return the interpolated frame (right channel left untouched when @p channels == 1).
	[[nodiscard]] StereoSample interpolate(std::size_t channels, int32_t whichKernel, uint32_t oscPos);

	/// @brief 2-tap linear interpolation between the two newest window samples — the cheap alternative.
	/// @param channels 1 = mono, 2 = stereo. @param phase the sub-sample phase.
	[[nodiscard]] StereoSample interpolateLinear(std::size_t channels, uint32_t phase);

	/// Advance the left window by one sample, inserting @p value at the head.
	[[gnu::always_inline]] constexpr void pushL(int16_t value) {
		shiftWindow(buffer_l, 1);
		buffer_l[0] = value;
	}
	/// Advance the right window by one sample, inserting @p value at the head.
	[[gnu::always_inline]] constexpr void pushR(int16_t value) {
		shiftWindow(buffer_r, 1);
		buffer_r[0] = value;
	}
	/// Advance both windows by @p numSamples, vacating that many head slots for the caller to fill.
	[[gnu::always_inline]] constexpr void jumpForward(std::size_t numSamples) {
		shiftWindow(buffer_l, numSamples);
		shiftWindow(buffer_r, numSamples);
	}

	/// The per-channel tap windows (newest sample at index 0). Quadword-aligned for the NEON dot product, and
	/// value-initialised so an unfed tap reads as a defined 0 rather than allocation residue.
	alignas(sizeof(int32_t) * 4) Window buffer_l{};
	alignas(sizeof(int32_t) * 4) Window buffer_r{};

private:
	/// Shift @p window toward older indices by @p n samples, vacating the @p n head slots (the discarded tail falls
	/// off the end). Shared by @ref pushL / @ref pushR / @ref jumpForward.
	[[gnu::always_inline]] static constexpr void shiftWindow(Window& window, std::size_t n) {
#pragma unroll
		for (int32_t i = kInterpolationMaxNumSamples - 1; i >= static_cast<int32_t>(n); i--) {
			window[i] = window[i - static_cast<int32_t>(n)];
		}
	}
};

} // namespace deluge::dsp::fixed

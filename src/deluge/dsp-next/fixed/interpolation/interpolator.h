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
#include <argon.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed {

namespace detail {
/// Shift a 16-tap window held as two 8-lane halves toward older indices by @p N (1 ... 15): the value at i ends up at
/// i + N and the oldest fall off. `vext` takes its offset as an immediate, hence the template. The @p N vacated head
/// slots come out holding unspecified values, which the caller overwrites.
template <int N>
[[gnu::always_inline]] inline void shift_window_by(Argon<int16_t>& low, Argon<int16_t>& high) {
	static_assert(N > 0 && N < 16);
	if constexpr (N < 8) {
		// The top half takes the upper end of the bottom half, the bottom half moves up within itself
		high = low.Extract<8 - N>(high);
		low = low.Extract<8 - N>(low);
	}
	else if constexpr (N == 8) {
		high = low;
	}
	else {
		// Only the top half keeps anything, taken from the bottom half
		high = low.Extract<16 - N>(low);
	}
}
} // namespace detail

/// @brief A stereo windowed-sinc / linear sample interpolator over a sliding tap window (q31 backend).
///
/// Holds the most recent @ref kInterpolationMaxNumSamples input samples per channel — the *tap window*, newest at
/// index 0, oldest at the end. The owner feeds the window (@ref push_l / @ref push_r advance it by one sample,
/// @ref jump_forward by several) and reads a sub-sample-accurate output frame with @ref interpolate (windowed-sinc,
/// using the @p whichKernel-selected oversampling variant of @ref kWindowedSincKernel) or, on the cheap path,
/// @ref interpolate_linear (2-tap linear).
///
/// The tap windows are intentionally public: the sample-playback reader fills and shifts them directly for loop
/// crossfades and jump-forward zero-fills — sample-specific window management that isn't generic interpolation, and
/// which a later refactor may lift behind a richer API. The feed/read methods are the common path.
struct Interpolator {
	using Window = std::array<int16_t, kInterpolationMaxNumSamples>;

	/// @brief Sub-sample-accurate windowed-sinc interpolation of the current window.
	/// @param channels 1 = mono (left window only), 2 = stereo (both).
	/// @param whichKernel the oversampling-kernel variant (from `WindowedSincKernel::variant_for`, by playback rate).
	/// @param oscPos the sub-sample phase within the window.
	/// @return the interpolated frame (right channel left untouched when @p channels == 1).
	[[nodiscard]] StereoSample interpolate(std::size_t channels, int32_t which_kernel, uint32_t osc_pos);

	/// @brief 2-tap linear interpolation between the two newest window samples — the cheap alternative.
	/// @param channels 1 = mono, 2 = stereo. @param phase the sub-sample phase.
	[[nodiscard]] StereoSample interpolate_linear(std::size_t channels, uint32_t phase);

	/// Advance the left window by one sample, inserting @p value at the head.
	[[gnu::always_inline]] constexpr void push_l(int16_t value) {
		shift_window(buffer_l, 1);
		buffer_l[0] = value;
	}
	/// Advance the right window by one sample, inserting @p value at the head.
	[[gnu::always_inline]] constexpr void push_r(int16_t value) {
		shift_window(buffer_r, 1);
		buffer_r[0] = value;
	}
	/// Advance both windows by @p num_samples, vacating that many head slots for the caller to fill.
	[[gnu::always_inline]] constexpr void jump_forward(std::size_t num_samples) {
		shift_window(buffer_l, num_samples);
		shift_window(buffer_r, num_samples);
	}

	/// The per-channel tap windows (newest sample at index 0). Quadword-aligned for the NEON dot product, and
	/// value-initialised so an unfed tap reads as a defined 0 rather than allocation residue.
	alignas(sizeof(int32_t) * 4) Window buffer_l{};
	alignas(sizeof(int32_t) * 4) Window buffer_r{};

private:
	/// Shift @p window toward older indices by @p n samples, vacating the @p n head slots (the discarded tail falls
	/// off the end; the vacated slots hold unspecified values until the caller fills them). Shared by @ref push_l /
	/// @ref push_r / @ref jump_forward. Two NEON `vext`s per window instead of up to 15 halfword copies, which neither
	/// GCC nor Clang vectorises for n < 8 (the loop reads slots it has just written).
	[[gnu::always_inline]] static constexpr void shift_window(Window& window, std::size_t n) {
		if consteval {
			for (int32_t i = kInterpolationMaxNumSamples - 1; i >= static_cast<int32_t>(n); i--) {
				window[i] = window[i - static_cast<int32_t>(n)];
			}
		}
		else {
			static_assert(kInterpolationMaxNumSamples == 2 * Argon<int16_t>::lanes);
			if (n == 0 || n >= kInterpolationMaxNumSamples) {
				return; // 0: nothing moves. 16 and more: nothing stays, the caller refills every slot
			}
			auto low = Argon<int16_t>::Load(window.data());
			auto high = Argon<int16_t>::Load(window.data() + Argon<int16_t>::lanes);
			switch (n) {
			case 1:
				detail::shift_window_by<1>(low, high);
				break;
			case 2:
				detail::shift_window_by<2>(low, high);
				break;
			case 3:
				detail::shift_window_by<3>(low, high);
				break;
			case 4:
				detail::shift_window_by<4>(low, high);
				break;
			case 5:
				detail::shift_window_by<5>(low, high);
				break;
			case 6:
				detail::shift_window_by<6>(low, high);
				break;
			case 7:
				detail::shift_window_by<7>(low, high);
				break;
			case 8:
				detail::shift_window_by<8>(low, high);
				break;
			case 9:
				detail::shift_window_by<9>(low, high);
				break;
			case 10:
				detail::shift_window_by<10>(low, high);
				break;
			case 11:
				detail::shift_window_by<11>(low, high);
				break;
			case 12:
				detail::shift_window_by<12>(low, high);
				break;
			case 13:
				detail::shift_window_by<13>(low, high);
				break;
			case 14:
				detail::shift_window_by<14>(low, high);
				break;
			case 15:
				detail::shift_window_by<15>(low, high);
				break;
			}
			low.StoreTo(window.data());
			high.StoreTo(window.data() + Argon<int16_t>::lanes);
		}
	}
};

} // namespace deluge::dsp::fixed

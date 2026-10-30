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

#include "dsp-next/fixed/fft/complex.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::dsp::fixed::fft {

inline constexpr int32_t kMinMagnitude = 3;        ///< the smallest transform the band builder runs: an 8-point inverse
inline constexpr int32_t kMinForwardMagnitude = 4; ///< the smallest forward: 16 points
inline constexpr int32_t kMaxMagnitude = 13;       ///< 8192 points: the largest cycle and the pitch window
inline constexpr int32_t kMaxFactors = 32;         ///< the factor list holds 2 × this words

/// @brief The tables of a 2^magnitude-point int32 real transform: the factor list of its complex half-length transform,
///        each stage's twiddles after the first, and the real split's twiddles. Views over storage the caller lends.
struct Twiddles {
	int32_t magnitude = 0;      ///< log2 of the real transform's length
	std::span<int32_t> factors; ///< stage count, first stride, (radix, remaining length) per stage, algorithm flag
	std::span<Complex> stage;   ///< stage twiddles, stage by stage from the second; 2^(magnitude-1) entries reserved
	std::span<Complex> super;   ///< 2^(magnitude-2) split twiddles

	/// @brief The complex half-length the transform runs at: 2^(magnitude-1).
	[[nodiscard]] int32_t complex_length() const { return int32_t{1} << (magnitude - 1); }
};

/// @brief A transform's tables and the scratch it works in, laid out over one block.
struct Layout {
	Twiddles twiddles;
	std::span<Complex> scratch; ///< 2^magnitude entries: two complex half-length buffers
};

/// @brief Bytes of one block holding a 2^magnitude transform's tables and scratch.
[[nodiscard]] constexpr std::size_t storage_bytes(int32_t magnitude) {
	const std::size_t half = std::size_t{1} << (magnitude - 1);
	return sizeof(int32_t) * kMaxFactors * 2 + sizeof(Complex) * (half + half / 2 + 2 * half);
}

/// @brief Lays the tables and scratch out over @p storage (at least storage_bytes(magnitude) bytes, 8-byte aligned).
[[nodiscard]] Layout lay_out(std::span<std::byte> storage, int32_t magnitude);

/// @brief Stage 1 of 3: the factor list (powers of two: radix 4 stages, a first stage of 8 when one is left over).
void factor(Twiddles& twiddles);

/// @brief Stage 2 of 3: the stage twiddles. Float phase and double cos/sin, in a fixed operation order the records
///        pin: the caller may yield after it.
void generate_stage_twiddles(Twiddles& twiddles);

/// @brief Stage 3 of 3: the real split's twiddles.
void generate_super_twiddles(Twiddles& twiddles);

} // namespace deluge::dsp::fixed::fft

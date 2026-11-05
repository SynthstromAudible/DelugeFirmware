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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "dsp-next/fixed/shaping/tanh_small_table.h"

#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief The table centre a silent sample maps to, 2^31: a sample's position on the curve is the sample, driven, plus
/// this. Shared by @ref tanh and the anti-aliased tanh, whose tables both span the 32-bit position range.
inline constexpr uint32_t kTanhCentre = 2147483648u;

namespace detail {
/// @brief A position on the curve (unsigned, the full table across 32 bits) looked up and shifted down by drive + 2.
/// @details The table's base is a constant, so the lookup makes no call to `std::array::data` where the app
/// instantiates it. The table holds Q15 tanh values; the interpolation lifts them to Q31, and the run-time shift
/// keeps the format: the level falls by 2^(drive + 2).
[[nodiscard]] [[gnu::always_inline]] inline Q<31> tanh_lookup(uint32_t position, uint32_t drive) {
	constexpr const int16_t* kBase = kTanhSmallTable.data();
	const auto level = Q<31>::from_raw(kLinearTableInterpolator.interpolate_signed(position, 32, kBase, 8));
	return scale_shr_floor(level, static_cast<int>(drive + 2));
}
} // namespace detail

/// @brief Tanh saturation without antialiasing memory: the input driven by 2^@p drive (saturating), looked up in
/// @ref kTanhSmallTable, and shifted down by drive + 2 — so the output's level falls as the drive rises (a quarter of
/// full scale undriven), and callers scale it back as their home needs.
/// @param sample The input, Q31.
/// @param drive The drive as a power of two, 0 to 29: the output shift, drive + 2, must stay below 32. Saturation is
/// at 32 - drive bits for drives 1 to 20 and at 12 bits for drives past 20; see the compile-time overload for those.
/// @return The saturated sample, Q31.
[[nodiscard]] [[gnu::always_inline]] inline Q<31> tanh(Q<31> sample, uint32_t drive) {
	// The table's position is the driven sample's word, unsigned, about the centre
	const Q<31> driven = drive != 0u ? scale_shl_clamp_then_shift(sample, static_cast<int>(drive)) : sample;
	const uint32_t position = static_cast<uint32_t>(driven.raw()) + kTanhCentre;
	return detail::tanh_lookup(position, drive);
}

/// @brief @ref tanh with the drive fixed at compile time: saturation is at exactly 32 - @p Drive bits for every drive
/// (the runtime form's saturation stops narrowing at 12 bits, so the two differ past a drive of 20).
/// @tparam Drive The drive as a power of two, 0 to 29: the output shift, Drive + 2, must stay below 32.
/// @param sample The input, Q31.
/// @return The saturated sample, Q31, shifted down by Drive + 2.
template <uint32_t Drive>
requires(Drive <= 29)
[[nodiscard]] [[gnu::always_inline]] inline Q<31> tanh(Q<31> sample) {
	uint32_t position{};
	if constexpr (Drive != 0u) {
		position = static_cast<uint32_t>(scale_shl_clamp_then_shift<Drive>(sample).raw()) + kTanhCentre;
	}
	else {
		position = static_cast<uint32_t>(sample.raw()) + kTanhCentre;
	}
	return detail::tanh_lookup(position, Drive);
}

} // namespace deluge::dsp::fixed

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

#include "dsp-next/core/types.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "dsp-next/fixed/shaping/tanh_small_table.h"
#include "util/param_functions.h" // lshiftAndSaturate, lshiftAndSaturateUnknown

#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief The table centre a silent sample maps to, 2^31: a sample's position on the curve is the sample, driven, plus
/// this. Shared by @ref tanh and the anti-aliased tanh, whose tables both span the 32-bit position range.
inline constexpr uint32_t kTanhCentre = 2147483648u;

namespace detail {
/// @brief A position on the curve (unsigned, the full table across 32 bits) looked up and shifted down by drive + 2.
/// @details The table's base is a constant, so the lookup makes no call to `std::array::data` where the app
/// instantiates it.
[[nodiscard]] [[gnu::always_inline]] inline q31 tanh_lookup(uint32_t position, uint32_t drive) {
	constexpr const int16_t* kBase = kTanhSmallTable.data();
	return linear_table_interpolator.interpolateSigned(position, 32, kBase, 8) >> (drive + 2);
}
} // namespace detail

/// @brief Tanh saturation without antialiasing memory: the input (Q31) driven by 2^@p drive (saturating), looked up
/// in @ref kTanhSmallTable, and shifted down by drive + 2 — so the output's level falls as the drive rises (Q31 at a
/// quarter of full scale undriven), and callers scale it back as their home needs.
/// @param drive The drive as a power of two, 0 to 29: the output shift, drive + 2, must stay below 32. Saturation is
/// at 32 - drive bits for drives 1 to 20 and at 12 bits for drives past 20; see the compile-time overload for those.
[[nodiscard]] [[gnu::always_inline]] inline q31 tanh(q31 sample, uint32_t drive) {
	const uint32_t position =
	    drive != 0u ? static_cast<uint32_t>(lshiftAndSaturateUnknown(sample, static_cast<uint8_t>(drive))) + kTanhCentre
	                : static_cast<uint32_t>(sample) + kTanhCentre;
	return detail::tanh_lookup(position, drive);
}

/// @brief @ref tanh with the drive fixed at compile time: saturation is at exactly 32 - @p Drive bits for every drive
/// (the runtime form's saturation stops narrowing at 12 bits, so the two differ past a drive of 20).
/// @tparam Drive The drive as a power of two, 0 to 29: the output shift, Drive + 2, must stay below 32.
template <uint32_t Drive>
requires(Drive <= 29)
[[nodiscard]] [[gnu::always_inline]] inline q31 tanh(q31 sample) {
	uint32_t position{};
	if constexpr (Drive != 0u) {
		position = static_cast<uint32_t>(lshiftAndSaturate<Drive>(sample)) + kTanhCentre;
	}
	else {
		position = static_cast<uint32_t>(sample) + kTanhCentre;
	}
	return detail::tanh_lookup(position, Drive);
}

} // namespace deluge::dsp::fixed

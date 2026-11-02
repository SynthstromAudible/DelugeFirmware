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
#include "dsp-next/fixed/curves/exp_curve.h"
#include "dsp-next/fixed/tables/release_rate.h"

#include <cstdint>

/// @file
/// @brief The curves that turn a param's combination into its final value around the param's neutral value.
///
/// A combination is Q29 around zero: 2^29 is "1", so a combination c scales by (1 + c / 2^29). Patching gives it
/// headroom to ±3 (several cables multiplied); the curves saturate rather than wrap.

namespace deluge::dsp::fixed {

/// @brief The curve that turns a param's combination into its final value.
enum class ParamCurve : uint8_t {
	SQUARE,               ///< neutral · (1 + c)²: the square law, up to 4 × neutral
	LINEAR,               ///< neutral · (1 + c), up to 2 × neutral
	BIPOLAR,              ///< a position around neutral, its combination doubled (pan, phase widths, wave indexes)
	EXPONENTIAL,          ///< neutral · 2^(c / 2^26 + 2): frequencies, pitches, rates
	EXPONENTIAL_INVERTED, ///< the exponential of −c: envelope attacks
	RELEASE_TABLE,        ///< the release-rate table: envelope decays and releases
};

/// @brief neutral · (1 + c)², saturating at the q31 range.
/// @param neutral The final value at a zero combination, in the result's units.
/// @param combination Q29: 2^29 is "1".
/// @return The final value, in the neutral's units.
[[nodiscard]] inline int32_t curve_square(int32_t neutral, int32_t combination) {
	// 0 ("0") to 2^30 ("2") ideally, up to 2^31 − 1 ("4") with several cables multiplied
	const Q<29> positive = add_wrap(Q<29>::from_raw(combination), q<29>(1.0));
	// The square, Q29 · Q29 >> 31: Q27
	const Q<27> square = Q<27>::from_raw((positive.raw() >> 16) * (positive.raw() >> 15));
	// The neutral read as Q31 makes the product Q26 of it; back in its units, saturating: mod FX depth's neutral
	// overflows easily
	return as_clamp_then_shift<Q<31>>(mul(square, Q<31>::from_raw(neutral))).raw();
}

/// @brief neutral · (1 + c), saturating at the q31 range.
/// @param neutral The final value at a zero combination, in the result's units.
/// @param combination Q29: 2^29 is "1".
/// @return The final value, in the neutral's units.
[[nodiscard]] inline int32_t curve_linear(int32_t neutral, int32_t combination) {
	const Q<29> positive = add_wrap(Q<29>::from_raw(combination), q<29>(1.0));
	// The neutral read as Q31 makes the product Q28 of it; back in its units, saturating: the sustain level overflows
	// easily
	return as_clamp_then_shift<Q<31>>(mul(positive, Q<31>::from_raw(neutral))).raw();
}

/// @brief A position around neutral: neutral + 2c, saturating at ±2^30 in steps of 4. Its ±2^30 is pan's full left
/// to right.
/// @param neutral The final value at a zero combination, Q31.
/// @param combination Q29: 2^29 is "1".
/// @return The final value, Q31.
[[nodiscard]] inline int32_t curve_bipolar(int32_t neutral, int32_t combination) {
	// A quarter of the neutral plus half the combination's raw value, at Q29: two shifted values, so no wrap
	const Q<29> pre_limits =
	    add_wrap(as_floor<Q<29>>(Q<31>::from_raw(neutral)), scale_shr_floor<1>(Q<29>::from_raw(combination)));
	return as_wrap<Q<31>>(clamp_bits<29>(pre_limits)).raw();
}

/// @brief neutral · 2^(c / 2^26 + 2), saturating (exp_curve).
/// @param neutral The final value at a zero combination, before the curve's × 4.
/// @param combination Q26 octaves.
/// @return The final value.
[[nodiscard]] inline int32_t curve_exponential(int32_t neutral, int32_t combination) {
	return exp_curve(neutral, combination);
}

/// @brief The release-rate table at @p input, interpolated linearly between its entries and clamped at its ends.
/// @param input Q24 table steps around the table's middle entry (32).
/// @return The rate, Q31 of the table's scale.
[[nodiscard]] inline int32_t release_rate(int32_t input) {
	constexpr int32_t kMagnitude = 24;
	// Indexed through a pointer taken at compile time, by subscripts that need no inlining
	constexpr const int32_t* kTable = tables::kReleaseRate.data();
	int32_t which = input >> kMagnitude;
	const Q<31> further = Q<31>::from_raw((input << (31 - kMagnitude)) & 2147483647);
	which += 32;
	if (which < 0) {
		return kTable[0];
	}
	if (which >= 64) {
		return kTable[64];
	}
	const Q<31> value1 = Q<31>::from_raw(kTable[which]);
	const Q<31> value2 = Q<31>::from_raw(kTable[which + 1]);
	// A convex blend of two Q31 entries, Q30 after the Q31 weights: the sum can't wrap
	const Q<30> blend = add_wrap(mul(value2, further), mul(value1, sub_wrap(Q<31>::max(), further)));
	return as_wrap<Q<31>>(blend).raw();
}

/// @brief The combination a stored word gives a param with no cables: the word (a q31 knob position) as Q29.
/// @param preset The stored word, Q31.
/// @return The combination, Q29.
[[nodiscard]] constexpr int32_t preset_combination(int32_t preset) {
	return preset >> 2;
}

} // namespace deluge::dsp::fixed

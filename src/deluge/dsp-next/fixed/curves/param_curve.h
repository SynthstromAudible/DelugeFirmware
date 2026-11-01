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

#include "dsp-next/core/q31_ops.h"
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
/// @param neutral The final value at a zero combination.
/// @param combination Q29: 2^29 is "1".
/// @return The final value.
[[nodiscard]] inline int32_t curve_square(int32_t neutral, int32_t combination) {
	// 0 ("0") to 2^30 ("2") ideally, up to 2^31 − 1 ("4") with several cables multiplied
	int32_t positive = combination + 536870912;
	// The square, Q29 · Q29 >> 31: Q27 ("1" is 2^27)
	positive = (positive >> 16) * (positive >> 15);
	// Saturating: mod FX depth's neutral overflows easily
	return lshift_and_saturate<5>(multiply_32x32_rshift32(positive, neutral));
}

/// @brief neutral · (1 + c), saturating at the q31 range.
/// @param neutral The final value at a zero combination.
/// @param combination Q29: 2^29 is "1".
/// @return The final value.
[[nodiscard]] inline int32_t curve_linear(int32_t neutral, int32_t combination) {
	const int32_t positive = combination + 536870912;
	// Saturating: the sustain level overflows easily
	return lshift_and_saturate<3>(multiply_32x32_rshift32(positive, neutral));
}

/// @brief A position around neutral: neutral + 2c, saturating at ±2^31 in steps of 4. Its ±2^30 is pan's full left
/// to right.
/// @param neutral The final value at a zero combination.
/// @param combination Q29: 2^29 is "1".
/// @return The final value.
[[nodiscard]] inline int32_t curve_bipolar(int32_t neutral, int32_t combination) {
	const int32_t pre_limits = (neutral >> 2) + (combination >> 1);
	return signed_saturate<32 - 3>(pre_limits) << 2;
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
	int32_t which = input >> kMagnitude;
	const int32_t further = (input << (31 - kMagnitude)) & 2147483647;
	which += 32;
	if (which < 0) {
		return tables::kReleaseRate[0];
	}
	if (which >= 64) {
		return tables::kReleaseRate[64];
	}
	const int32_t value1 = tables::kReleaseRate[which];
	const int32_t value2 = tables::kReleaseRate[which + 1];
	return (multiply_32x32_rshift32(value2, further) + multiply_32x32_rshift32(value1, 2147483647 - further)) << 1;
}

/// @brief The combination a stored word gives a param with no cables: the word (a q31 knob position) as Q29.
/// @param preset The stored word, Q31.
/// @return The combination, Q29.
[[nodiscard]] constexpr int32_t preset_combination(int32_t preset) {
	return preset >> 2;
}

} // namespace deluge::dsp::fixed

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
/// @param combination 2^29 is "1".
/// @return The final value, in the neutral's format.
template <int F>
[[nodiscard]] inline Q<F> curve_square(Q<F> neutral, Q<29> combination) {
	// 0 ("0") to 2^30 ("2") ideally, up to 2^31 − 1 ("4") with several cables multiplied
	const Q<29> positive = add_wrap(combination, q<29>(1.0));
	// The square, Q29 · Q29 >> 31: Q27
	const Q<27> square = Q<27>::from_raw((positive.raw() >> 16) * (positive.raw() >> 15));
	// Back in the neutral's format, saturating: mod FX depth's neutral overflows easily
	return as_clamp_then_shift<Q<F>>(mul(square, neutral));
}

/// @brief neutral · (1 + c), saturating at the q31 range.
/// @param neutral The final value at a zero combination, in the result's units.
/// @param combination 2^29 is "1".
/// @return The final value, in the neutral's format.
template <int F>
[[nodiscard]] inline Q<F> curve_linear(Q<F> neutral, Q<29> combination) {
	const Q<29> positive = add_wrap(combination, q<29>(1.0));
	// Back in the neutral's format, saturating: the sustain level overflows easily
	return as_clamp_then_shift<Q<F>>(mul(positive, neutral));
}

/// @brief A position around neutral: neutral + 2c, saturating at ±2^30 in steps of 4. Its ±2^30 is pan's full left
/// to right.
/// @param neutral The final value at a zero combination.
/// @param combination 2^29 is "1".
/// @return The final value.
[[nodiscard]] inline Q<31> curve_bipolar(Q<31> neutral, Q<29> combination) {
	// A quarter of the neutral plus half the combination's raw value, at Q29: two shifted values, so no wrap
	const Q<29> pre_limits = add_wrap(as_floor<Q<29>>(neutral), scale_shr_floor<1>(combination));
	return as_wrap<Q<31>>(clamp_bits<29>(pre_limits));
}

/// @brief neutral · 2^(c / 2^26 + 2), saturating (exp_curve).
/// @param neutral The final value at a zero combination, before the curve's × 4.
/// @param combination Read as octaves at 2^26 each: "1" is 8 octaves.
/// @return The final value, in the neutral's format.
template <int F>
[[nodiscard]] inline Q<F> curve_exponential(Q<F> neutral, Q<29> combination) {
	// The combination's word is the curve's octaves
	return exp_curve(neutral, reinterpret_bits<Q<26>>(combination));
}

/// @brief The release-rate table at @p input, interpolated linearly between its entries and clamped at its ends.
/// @param combination Read as table steps at 2^24 each around the table's middle entry (32): "1" is 32 steps.
/// @return The rate, of the table's scale.
[[nodiscard]] inline Q<31> release_rate(Q<29> combination) {
	constexpr int32_t kMagnitude = 24;
	constexpr const auto& kTable = tables::kReleaseRate;
	// The combination's word is the table steps: the step (high bits) and the fraction toward the next (low bits)
	const int32_t input = combination.raw();
	int32_t which = input >> kMagnitude;
	const Q<31> further = Q<31>::from_raw((input << (31 - kMagnitude)) & 2147483647);
	which += 32;
	if (which < 0) {
		return Q<31>::from_raw(kTable[0]);
	}
	if (which >= 64) {
		return Q<31>::from_raw(kTable[64]);
	}
	const Q<31> value1 = Q<31>::from_raw(kTable[which]);
	const Q<31> value2 = Q<31>::from_raw(kTable[which + 1]);
	// A convex blend of two Q31 entries, Q30 after the Q31 weights: the sum can't wrap
	const Q<30> blend = add_wrap(mul(value2, further), mul(value1, sub_wrap(Q<31>::max(), further)));
	return as_wrap<Q<31>>(blend);
}

/// @brief The combination a stored word gives a param with no cables: the word (a knob position) at Q29.
/// @param preset The stored word.
/// @return The combination.
[[nodiscard]] constexpr Q<29> preset_combination(Q<31> preset) {
	return as_floor<Q<29>>(preset);
}

} // namespace deluge::dsp::fixed

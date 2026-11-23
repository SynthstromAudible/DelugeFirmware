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

#include "dsp/core/fixed_point.h"
#include "dsp/fixed/interpolation/table_interpolation.h"
#include "dsp/fixed/tables/cent_adjust.h"

#include <algorithm>
#include <cstdint>

/// @file
/// @brief A small pitch offset applied to a phase increment.

namespace deluge::dsp::fixed {

/// @brief A pitch offset of up to a quarter tone either way, as a ratio a phase increment is multiplied by.
class FineTune {
public:
	/// @brief The step one cent makes: a quarter tone (50 cents) either way spans the Q31 range of a step.
	static constexpr int32_t kCentStep = 42949672;
	/// @brief The most a fine tune reaches either way, in cents.
	static constexpr int32_t kMaxCents = 50;

	/// @brief No offset: a ratio of 1.0.
	constexpr FineTune() noexcept = default;

	/// @brief The offset of @p cents, held to ±50 (past that, cents × kCentStep overflows).
	[[nodiscard]] static FineTune of_cents(int32_t cents) noexcept { return of_step(step_of(cents)); }

	/// @brief The offset of a detune step: Q31 of the ±50-cent span, so ±2^31 is ±50 cents.
	/// @param step The step, unclamped: unison detunes are steps between the parts' cents.
	[[nodiscard, gnu::always_inline]] static FineTune of_step(int32_t step) noexcept {
		FineTune tune;
		// The step offset to an unsigned index across the table: 0 cents is its middle. The table's UQ1.15 ratio
		// times the interpolator's Q15 blend is the Q2.30 multiplier.
		const uint32_t index = (uint32_t{1} << 31) + static_cast<uint32_t>(step);
		// Crossing: the table interpolator returns its blend as a word.
		tune.multiplier_ = Q<30>::from_raw(kLinearTableInterpolator.interpolate(index, 32, tables::kCentAdjust.data()));
		return tune;
	}

	/// @brief The detune step @p cents makes, held to ±50 cents.
	[[nodiscard]] static constexpr int32_t step_of(int32_t cents) noexcept {
		return std::clamp(cents, -kMaxCents, kMaxCents) * kCentStep;
	}

	/// @brief @p increment offset by this tune.
	/// @param increment A phase increment (UQ0.32 of a cycle per sample) below 2^31.
	/// @return The increment read as Q31 times the Q2.30 multiplier, back at Q31: increments stay under 2^31, so the
	///         bits agree with the unsigned increment.
	[[nodiscard, gnu::always_inline]] int32_t apply(int32_t increment) const noexcept {
		// Crossings: a phase increment is a word interface (the engines hold increments as unsigned words).
		return mul_as_wrap<Q<31>>(Q<31>::from_raw(increment), multiplier_).raw();
	}

	bool operator==(const FineTune&) const = default;

private:
	Q<30> multiplier_{q<30>(1.0)}; ///< the ratio the increment is multiplied by
};

} // namespace deluge::dsp::fixed

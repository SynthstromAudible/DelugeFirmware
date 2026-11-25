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
#pragma once

#include "dsp/core/pack_f32.h"
#include "dsp/core/sample.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <numbers>

/// @file
/// @brief main's envelope curve in float: a falling curve of k octaves, normalised to run from 1 to 0 whatever its
/// shape, its inverse, and the exponential under it, over V.
namespace deluge::dsp::fp {

namespace detail {
/// @brief 2^w for the whole @p rounded - @p bias, both as the float sum that rounds to a whole number leaves them.
[[nodiscard]] [[gnu::always_inline]] inline f32 exp2_whole(f32 rounded, f32 bias) {
	const int32_t w = std::bit_cast<int32_t>(rounded) - std::bit_cast<int32_t>(bias);
	return std::bit_cast<f32>((w + 127) << 23);
}
/// @brief As above, in each lane.
template <std::size_t N>
[[nodiscard]] [[gnu::always_inline]] inline Pack<f32, N> exp2_whole(Pack<f32, N> rounded, f32 bias) {
	using Ints = deluge::dsp::detail::I32Lanes<N>;
	const Ints w = std::bit_cast<Ints>(rounded.v) - std::bit_cast<int32_t>(bias);
	return Pack<f32, N>{std::bit_cast<typename Pack<f32, N>::Lanes>((w + 127) << 23)};
}
} // namespace detail

/// @brief 2^y - 1, accurate relative to itself however near 0 @p y is; |y| below 126.
/// @details y = w + f with w whole and |f| <= 1/2. `2^f - 1` is its series without the constant term, so where w is 0
/// nothing cancels; elsewhere `2^w·(2^f - 1) + (2^w - 1)`. Rounded by the float's own last bit, with no compare.
template <class V>
[[nodiscard]] [[gnu::always_inline]] inline V exp2m1(V y) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	constexpr f32 kRound = 12582912.f; // 1.5 * 2^23
	const V rounded = y + V{kRound};
	const V f = y - (rounded - V{kRound});
	// ln2^n / n!, n = 7 down to 1
	V p{1.5252734e-5f};
	p = (p * f) + V{1.5403530e-4f};
	p = (p * f) + V{1.3333558e-3f};
	p = (p * f) + V{9.6181291e-3f};
	p = (p * f) + V{5.5504109e-2f};
	p = (p * f) + V{0.24022651f};
	p = (p * f) + V{std::numbers::ln2_v<f32>};
	const V fraction = p * f;
	const V power = detail::exp2_whole(rounded, kRound);
	return (fraction * power) + (power - V{1.f});
}

/// @brief What a curve of @p k octaves divides by, as a factor: 1 / (2^k - 1); 0 at k = 0.
[[nodiscard]] inline f32 curve_scale(f32 k) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	return k == 0.f ? 0.f : 1.f / exp2m1(k);
}

/// @brief A falling stage from 1 at x = 0 to 0 at x = 1, `(2^(-k·x) - 2^-k) / (1 - 2^-k)`; `1 - x` at k = 0.
/// @details Written as `(2^(k·(1 - x)) - 1) / (2^k - 1)`, the same curve without the cancellation that costs a gentle
/// one its precision. Both ends are exact.
[[nodiscard]] inline f32 curve_falling(f32 k, f32 x) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	if (x <= 0.f) {
		return 1.f;
	}
	if (x >= 1.f) {
		return 0.f;
	}
	return k == 0.f ? 1.f - x : exp2m1(k * (1.f - x)) * curve_scale(k);
}

/// @brief How much of the stage is left, 1 to 0, when the falling curve of @p k octaves is at @p level (1 to 0), by
/// bisection; accurate relative to itself as it nears 0.
[[nodiscard]] inline f32 curve_remaining(f32 k, f32 level) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	if (level <= 0.f) {
		return 0.f;
	}
	if (level >= 1.f) {
		return 1.f;
	}
	const f32 scale = curve_scale(k);
	f32 low = 0.f;
	f32 high = 1.f;
	for (int32_t i = 0; i < 48; ++i) {
		const f32 middle = 0.5f * (low + high);
		((k == 0.f ? middle : exp2m1(k * middle) * scale) < level ? low : high) = middle;
	}
	return high;
}

/// @brief Where a rising stage of @p k octaves is when it reaches @p level (0 to 1), as a float and what it rounds off.
/// @details Measured from the end it is flat at, the start for a curve below 0 and the end for one above, where a level
/// fixes a position loosely: so a level near that end gives its position exactly.
struct CurvePosition {
	f32 phase; ///< 0 to 1.
	f32 rest;  ///< What `phase` rounds off.
};
[[nodiscard]] inline CurvePosition curve_position(f32 k, f32 level) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	if (k < 0.f) { // the rise of k is the fall of -k, mirrored
		return {.phase = curve_remaining(0.f - k, level), .rest = 0.f};
	}
	const f32 left = curve_remaining(k, 1.f - level);
	const f32 phase = 1.f - left;
	return {.phase = phase, .rest = (1.f - phase) - left};
}

} // namespace deluge::dsp::fp

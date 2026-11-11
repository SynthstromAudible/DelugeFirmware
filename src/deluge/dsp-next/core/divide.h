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

#include <array>
#include <bit>
#include <cstdint>

namespace deluge::dsp {

namespace detail {
inline constexpr int32_t kMaxSmallDivisor = 256;

// For each divisor n, m = ceil(2^(31 + l) / n) with l = bit_width(n - 1). By Granlund & Montgomery,
// floor(m * a / 2^(31 + l)) = floor(a / n) for every 0 <= a < 2^31, and m fits in 32 bits.
inline constexpr std::array<uint32_t, kMaxSmallDivisor + 1> kSmallDivisorMagic = [] {
	std::array<uint32_t, kMaxSmallDivisor + 1> magic{};
	for (uint32_t n = 2; n <= kMaxSmallDivisor; n++) {
		const uint32_t l = std::bit_width(n - 1);
		magic[n] = static_cast<uint32_t>(((uint64_t{1} << (31 + l)) + n - 1) / n);
	}
	return magic;
}();
} // namespace detail

/// @brief @p x / @p divisor, rounding towards zero like the operator, without a call to the software divide for
///        divisors up to 256 (the Cortex-A9 has no integer divide instruction).
///
/// A multiply by a reciprocal from a table, then a shift. Meant for per-block ramps, where the divisor is the block's
/// length in samples; other divisors fall back to the operator.
/// @param x The dividend; any value but INT32_MIN.
/// @param divisor The divisor, at least 1.
/// @return The quotient.
[[nodiscard]] [[gnu::always_inline]] inline int32_t divide_by_small(int32_t x, int32_t divisor) {
	using detail::kMaxSmallDivisor;
	if (static_cast<uint32_t>(divisor - 2) > static_cast<uint32_t>(kMaxSmallDivisor - 2)) [[unlikely]] {
		return (divisor == 1) ? x : x / divisor;
	}
	const uint32_t magnitude = (x < 0) ? 0u - static_cast<uint32_t>(x) : static_cast<uint32_t>(x);
	const auto high =
	    static_cast<uint32_t>((static_cast<uint64_t>(detail::kSmallDivisorMagic[divisor]) * magnitude) >> 32);
	const uint32_t quotient = high >> (std::bit_width(static_cast<uint32_t>(divisor - 1)) - 1);
	return (x < 0) ? -static_cast<int32_t>(quotient) : static_cast<int32_t>(quotient);
}

} // namespace deluge::dsp

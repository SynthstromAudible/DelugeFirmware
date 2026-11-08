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

/// @file
/// @brief Library-free coefficient math for the float kernels: plain double arithmetic in a fixed order, so every
/// target computes the same bits (a library exp or tan may differ between libms).

#include <bit>
#include <cassert>
#include <cstdint>
#include <numbers>

namespace deluge::dsp::fp::detail {

/// @brief e^(-x) for x >= 0, in double, within 1e-14 relative of the true value (measured 3.0e-16 on (0, 1] and
/// 2.2e-16 on (1, 700)); 0 from x = 700, where the true value is below 1e-304.
/// @details On [0, 1] a 20-term Taylor series in Horner form. Beyond 1 the argument is reduced as k·ln2 + r with
/// ln2 split in two so k·ln2_hi is exact, the same series gives e^(-r) on [0, ln2), and 2^-k is built from its bits.
[[nodiscard]] inline double exp_negative(double x) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	assert(x >= 0.0);
	double r = x;
	int k = 0;
	if (x > 1.0) {
		if (x >= 700.0) {
			return 0.0;
		}
		constexpr double kInvLn2 = std::numbers::log2e;
		constexpr double kLn2Hi = 6.93147180369123816490e-01; // NOLINT(modernize-use-std-numbers): ln2's high bits;
		                                                      // trailing zero bits: k·kLn2Hi is exact for k < 2^11
		constexpr double kLn2Lo = 1.90821492927058770002e-10;
		k = static_cast<int>(x * kInvLn2);
		r = (x - (static_cast<double>(k) * kLn2Hi)) - (static_cast<double>(k) * kLn2Lo);
	}
	double s = 1.0;
	for (int n = 20; n >= 1; --n) {
		s = 1.0 - ((s * r) / static_cast<double>(n));
	}
	if (k == 0) {
		return s;
	}
	const auto scale = std::bit_cast<double>(static_cast<uint64_t>(1023 - k) << 52); // 2^-k, k <= 1010
	return s * scale;
}

/// @brief tan(π·@p fc) for @p fc in (0, 0.49], in double, within 1e-14 relative of std::tan(π·fc) (measured 3.5e-15,
/// almost all of it the rounding of π·fc in that reference near 0.49, where tan amplifies it 32-fold).
/// @details With x = π·fc up to 0.25 the sine and the cosine are Taylor series in Horner form, 9 terms each (sine
/// through x^17, cosine through x^16; for x <= π/4 the first omitted terms are below 1e-19 and 2e-18 of the
/// result). Above 0.25 the cotangent of y = π·(0.5 − fc) is taken, y <= π/4 and 0.5 − fc exact, so neither series
/// is ever summed near a zero and the quotient keeps its relative accuracy.
[[nodiscard]] inline double tan_pi(double fc) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	assert(fc > 0.0 && fc <= 0.4900001); // 0.49 as an f32 is just above 0.49
	constexpr double kPi = std::numbers::pi;
	const bool upper = fc > 0.25;
	const double x = kPi * (upper ? 0.5 - fc : fc);
	const double x2 = x * x;
	double sine = 1.0;
	double cosine = 1.0;
	for (int n = 8; n >= 1; --n) {
		sine = 1.0 - ((sine * x2) / static_cast<double>((2 * n) * (2 * n + 1)));
		cosine = 1.0 - ((cosine * x2) / static_cast<double>((2 * n - 1) * (2 * n)));
	}
	sine = sine * x;
	return upper ? cosine / sine : sine / cosine;
}

} // namespace deluge::dsp::fp::detail

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
#include "dsp/core/divide.h"
#include <cppspec.hpp>
#include <cstdint>
#include <limits>

#include "support/spec_main.h"

using deluge::dsp::divide_by_small;

namespace {
// The first divisor and dividend where divide_by_small and the operator differ, or {0, 0} if none.
struct Mismatch {
	int32_t divisor;
	int32_t x;
};

Mismatch first_mismatch(int32_t max_divisor) {
	constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
	for (int32_t d = 1; d <= max_divisor; d++) {
		// Every dividend near the edges and near each multiple of the divisor in a sweep of the range
		auto check = [&](int64_t x) -> bool {
			if (x < -kMax || x > kMax) {
				return true;
			}
			const auto v = static_cast<int32_t>(x);
			return divide_by_small(v, d) == v / d;
		};
		for (int64_t x = -kMax; x <= kMax; x += 104'729) {
			for (int64_t k = -2; k <= 2; k++) {
				const int64_t near = (x / d) * d + k;
				if (!check(near)) {
					return {d, static_cast<int32_t>(near)};
				}
			}
		}
		for (int64_t x : {int64_t{0}, int64_t{1}, int64_t{-1}, int64_t{kMax}, int64_t{kMax - 1}, int64_t{-kMax},
		                  int64_t{-kMax + 1}}) {
			if (!check(x)) {
				return {d, static_cast<int32_t>(x)};
			}
		}
	}
	return {0, 0};
}
} // namespace

// clang-format off
describe divide("deluge::dsp::divide_by_small", ${
	it("matches the operator, rounding towards zero, for divisors up to 256 and past them", _ {
		const Mismatch m = first_mismatch(300);
		expect(m.divisor).to_equal(0);
		expect(m.x).to_equal(0);
	});

	it("rounds a negative quotient towards zero", _ {
		expect(divide_by_small(-7, 2)).to_equal(-3);
		expect(divide_by_small(-1, 128)).to_equal(0);
		expect(divide_by_small(-128, 128)).to_equal(-1);
	});
});
// clang-format on

DELUGE_SPEC(divide_spec, divide)

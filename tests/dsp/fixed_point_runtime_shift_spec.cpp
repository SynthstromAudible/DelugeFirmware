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
#include "dsp-next/core/fixed_point.h"
#include "raw_words.h"
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace deluge::dsp;

namespace {
// The edge words and a xorshift spread across the rest.
std::vector<int32_t> words() {
	std::vector<int32_t> w{INT32_MIN, INT32_MIN + 1, -2, -1, 0, 1, 2, INT32_MAX - 1, INT32_MAX};
	uint32_t s = 0x2468'ace1u;
	for (int i = 0; i < 4096; ++i) {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		w.push_back(static_cast<int32_t>(s));
	}
	return w;
}

std::size_t shr_mismatches() {
	std::size_t bad = 0;
	for (const int32_t x : words()) {
		for (int n = 0; n < 32; ++n) {
			bad += scale_shr_floor(Q<31>::from_raw(x), n).raw() != (x >> n) ? 1 : 0;
		}
	}
	return bad;
}

// The raw saturating shift, every shift its uint8_t takes below 32, the zero shift and the wrapping ones included.
std::size_t clamp_shift_mismatches() {
	std::size_t bad = 0;
	for (const int32_t x : words()) {
		for (int n = 0; n < 32; ++n) {
			bad += scale_shl_clamp_then_shift(Q<31>::from_raw(x), n).raw()
			               != lshift_and_saturate_unknown(x, static_cast<uint8_t>(n))
			           ? 1
			           : 0;
		}
	}
	return bad;
}

std::size_t pow2_mismatches() {
	std::size_t bad = 0;
	for (const int32_t x : words()) {
		for (int m = -31; m < 32; ++m) {
			bad += scale_pow2_sat(Q<31>::from_raw(x), m).raw() != increase_magnitude_and_saturate(x, m) ? 1 : 0;
		}
	}
	return bad;
}
} // namespace

// clang-format off
describe fixed_point_runtime_shift("the run-time shifts", ${
	it("floor-shift right by a run-time count as >> does", _ { expect(shr_mismatches()).to_equal(std::size_t{0}); });
	it("clamp then shift left as the raw saturating shift does", _ {
		expect(clamp_shift_mismatches()).to_equal(std::size_t{0});
	});
	it("scale by a signed power of two as the raw magnitude scale does", _ {
		expect(pow2_mismatches()).to_equal(std::size_t{0});
	});
});
// clang-format on

CPPSPEC_MAIN(fixed_point_runtime_shift);

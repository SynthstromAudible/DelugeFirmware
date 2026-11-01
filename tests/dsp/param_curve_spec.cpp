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

// The param curves in dsp-next against verbatim copies of the app's functions they replace.

#include "dsp-next/fixed/curves/param_curve.h"
#include "param_curve_reference.h"
#include "synth/matrix/descriptors.h"

#include <cppspec.hpp>
#include <cstdint>
#include <vector>

namespace curves = deluge::dsp::fixed;
namespace ref = param_curve_reference;

namespace {
/// Words a sweep can't miss: both ends, around zero and the half, and a deterministic spread between.
template <class F>
void each_word(F f) {
	for (int64_t v : {int64_t{INT32_MIN}, int64_t{INT32_MIN} + 1, int64_t{-1}, int64_t{0}, int64_t{1}, int64_t{1} << 29,
	                  -(int64_t{1} << 29), int64_t{1} << 30, (int64_t{1} << 30) - 1, int64_t{INT32_MAX} - 536870912,
	                  -(int64_t{1} << 30), int64_t{INT32_MAX} - (1 << 24), int64_t{INT32_MAX}}) {
		f(static_cast<int32_t>(v));
	}
	uint32_t x = 0x9E3779B9u;
	for (int i = 0; i < 20'000; ++i) {
		x = x * 1664525u + 1013904223u;
		f(static_cast<int32_t>(x));
	}
}

constexpr int32_t kMaxProductCombination = INT32_MAX - 536870912;

std::vector<int32_t> neutrals() {
	std::vector<int32_t> out{0, 1, INT32_MAX};
	for (int32_t n : deluge::synth::matrix::kParamNeutral) {
		out.push_back(n);
	}
	return out;
}
} // namespace

// clang-format off
describe param_curve_spec("the param curves", ${
	it("give today's final values for every neutral and combination", _ {
		int32_t bad = 0;
		for (int32_t n : neutrals()) {
			each_word([&](int32_t c) {
				// The square and linear curves add "1" (2^29) first: combinations reach 3 at most, never past
				if (c <= kMaxProductCombination) {
					bad += curves::curve_square(n, c) != ref::getFinalParameterValueVolume(n, c);
					bad += curves::curve_linear(n, c) != ref::getFinalParameterValueLinear(n, c);
				}
				bad += curves::curve_bipolar(n, c) != ref::getFinalParameterValueHybrid(n, c);
				bad += curves::curve_exponential(n, c) != ref::getFinalParameterValueExp(n, c);
			});
		}
		expect(bad).to_equal(0);
	});
	it("reads the release-rate table as today", _ {
		int32_t bad = 0;
		each_word([&](int32_t v) { bad += curves::release_rate(v) != ref::lookupReleaseRate(v); });
		for (int32_t step = -40; step <= 40; ++step) {
			bad += curves::release_rate(step << 24) != ref::lookupReleaseRate(step << 24);
		}
		expect(bad).to_equal(0);
	});
	it("takes a preset word as a quarter-scale combination", _ {
		expect(curves::preset_combination(INT32_MIN)).to_equal(INT32_MIN >> 2);
		expect(curves::preset_combination(-1)).to_equal(-1);
		expect(curves::preset_combination(INT32_MAX)).to_equal(INT32_MAX >> 2);
	});
});
// clang-format on

CPPSPEC_MAIN(param_curve_spec);

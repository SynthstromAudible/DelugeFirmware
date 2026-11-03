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

// curve_frequency: the filters' cutoff warp (filter_modes.h) against its verbatim legacy copy (reference_filters.h),
// field by field, over every 2^16th cutoff and the edges.

#include "dsp-next/fixed/filter/filter_modes.h"

#include "reference_filters.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

using deluge::dsp::q31;
using deluge::dsp::fixed::curve_frequency;

namespace {

/// Every 2^16th positive cutoff, then 1, 2^26 (the top of the tan table) and INT32_MAX.
std::vector<q31> cutoffs() {
	std::vector<q31> v;
	for (int32_t k = 1; k < (1 << 15); ++k) {
		v.push_back(k << 16);
	}
	v.insert(v.end(), {1, 1 << 26, INT32_MAX});
	return v;
}

/// Cutoffs where any field parts from the reference's.
std::size_t partings(const std::vector<q31>& frequencies) {
	std::size_t parted = 0;
	for (const q31 frequency : frequencies) {
		const auto curved = curve_frequency(frequency);
		const filters_reference::CurvedFrequency reference = filters_reference::curve_frequency(frequency);
		parted += curved.tanned.raw() != reference.tanned
		                  || curved.divide_by_one_plus_tanned.raw() != reference.divide_by_one_plus_tanned
		                  || curved.fc.raw() != reference.fc
		              ? 1
		              : 0;
	}
	return parted;
}

} // namespace

// clang-format off
describe curve_frequency_spec("curve_frequency", ${
	it("warps every 2^16th cutoff and the edges as the legacy warp does", _ {
		expect(partings(cutoffs())).to_equal(std::size_t{0});
	});
	// Cutoff 0 divides one at Q58 by one at Q27: 2^31, one past q31's top. The A9's VCVT saturates it to INT32_MAX,
	// one at Q31, and so does every target.
	it("gives cutoff 0 a reciprocal of one, saturated, and no moveability, on every target", _ {
		const auto curved = curve_frequency(0);
		expect(curved.tanned.raw()).to_equal(0);
		expect(curved.divide_by_one_plus_tanned.raw()).to_equal(INT32_MAX);
		expect(curved.fc.raw()).to_equal(0);
	});
	it("warps cutoff 0 as the legacy warp does on the A9", _ {
		expect(partings({0})).to_equal(std::size_t{0});
	});
});
// clang-format on

CPPSPEC_MAIN(curve_frequency_spec);

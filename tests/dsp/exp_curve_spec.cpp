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

// exp_curve: the exponential curve every param is shaped by — a preset scaled by 2^(adjustment / 2^26 + 2).

#include "dsp-next/fixed/curves/exp_curve.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>

using deluge::dsp::fixed::exp_curve;

// clang-format off
describe exp_curve_spec("exp_curve", ${
	it("scales by four at no adjustment", _ {
		expect(exp_curve(4194304, 0)).to_equal(4194304);
	});
	it("doubles or halves per octave of adjustment", _ {
		expect(exp_curve(4194304, 1 << 26)).to_equal(8388608);
		expect(exp_curve(4194304, -(1 << 26))).to_equal(2097152);
		expect(exp_curve(4194304, (1 << 26) * 5)).to_equal(134217728);
	});
	it("interpolates between octaves exactly", _ {
		expect(exp_curve(4194304, 12345678)).to_equal(4764748);
		expect(exp_curve(4194304, -98765432)).to_equal(1512278);
		expect(exp_curve(120000000, 50331648)).to_equal(201815184);
	});
	it("saturates rather than wraps when the adjustment asks for more than 20 octaves", _ {
		// A cutoff's add chain saturated by several full-depth cables: 33 octaves
		expect(exp_curve(4194304, std::numeric_limits<int32_t>::max())).to_equal(std::numeric_limits<int32_t>::max());
		// Up to 20 octaves the saturated ceiling, 2^31 - 2^octaves, steps down an octave at a time; nothing else falls
		constexpr int32_t kLowestCeiling = std::numeric_limits<int32_t>::max() - (1 << 20) + 1;
		int32_t previous = 0;
		int32_t falls = 0;
		int32_t negatives = 0;
		for (int64_t adjustment = 0; adjustment <= std::numeric_limits<int32_t>::max(); adjustment += 1 << 22) {
			const int32_t value = exp_curve(4194304, static_cast<int32_t>(adjustment));
			negatives += value < 0 ? 1 : 0;
			falls += (value < previous && value < kLowestCeiling) ? 1 : 0;
			previous = value;
		}
		expect(negatives).to_equal(0);
		expect(falls).to_equal(0);
	});
	it("scales exactly past 20 octaves while the result fits", _ {
		// 24 octaves (adjustment 22 << 26, no fraction) of a preset small enough to fit: 2 << 24
		expect(exp_curve(2, 22 << 26)).to_equal(0);
		expect(exp_curve(8, 22 << 26)).to_equal(1 << 25);
	});
});

CPPSPEC_MAIN(exp_curve_spec);

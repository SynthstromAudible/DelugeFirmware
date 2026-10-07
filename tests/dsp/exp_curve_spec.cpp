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
});

CPPSPEC_MAIN(exp_curve_spec);

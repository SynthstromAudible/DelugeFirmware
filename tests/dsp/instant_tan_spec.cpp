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

// instant_tan: tan of a Q31 angle by table (the ladders' frequency warp). The values were recorded from the app's
// instantTan.

#include "curves_reference.h"
#include "dsp-next/fixed/curves/instant_tan.h"
#include "raw_curves.h"

#include <cppspec.hpp>
#include <cstdint>

using deluge::dsp::Q;
using deluge::dsp::fixed::kTanTable;
using deluge::dsp::test::raw_curves::instant_tan;

// clang-format off
describe instant_tan_spec("instant_tan", ${
	it("gives a Q28 tangent of a Q31 angle", _ {
		expect(deluge::dsp::fixed::instant_tan(Q<31>::from_raw(1 << 29)).raw()).to_equal(101039870);
	});
	it("is zero at zero", _ {
		expect(instant_tan(0)).to_equal(0);
	});
	it("interpolates between table entries exactly", _ {
		expect(instant_tan(1 << 20)).to_equal(188774);
		expect(instant_tan(1 << 26)).to_equal(12087754);
		expect(instant_tan(1 << 29)).to_equal(101039870);
		expect(instant_tan(0x3FFFFFFF)).to_equal(235436120);
	});
	it("gives the reference's value at every angle", _ {
		int32_t bad = 0;
		for (int64_t angle = 0; angle <= INT32_MAX; angle += 997) {
			bad += instant_tan(static_cast<int32_t>(angle)) != curves_reference::instant_tan(static_cast<int32_t>(angle));
		}
		for (int32_t angle : {0, (1 << 25) - 1, 1 << 25, INT32_MAX}) {
			bad += instant_tan(angle) != curves_reference::instant_tan(angle);
		}
		expect(bad).to_equal(0);
	});
	it("reads the table's entries at Q28", _ {
		expect(instant_tan(0)).to_equal(kTanTable[0]);
	});
});

CPPSPEC_MAIN(instant_tan_spec);

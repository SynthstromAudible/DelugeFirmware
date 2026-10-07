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

// quick_log: a fast log2 approximation of an unsigned value — the magnitude in the bits from 25 up, the bits below the
// leading one beneath it. The values were recorded from the app's quickLog before the move.

#include "dsp-next/fixed/curves/quick_log.h"

#include <cppspec.hpp>
#include <cstdint>

using deluge::dsp::fixed::quick_log;

// clang-format off
describe quick_log_spec("quick_log", ${
	it("steps by 2^25 per octave", _ {
		expect(quick_log(1)).to_equal(67108864);
		expect(quick_log(2)).to_equal(100663296);
		expect(quick_log(1 << 20)).to_equal(738197504);
	});
	it("fills in between octaves from the bits below the leading one", _ {
		expect(quick_log(3)).to_equal(117440512);
		expect(quick_log(1000)).to_equal(401080320);
	});
	it("handles the full unsigned range", _ {
		expect(quick_log(0xFFFFFFFF)).to_equal(1006632959);
	});
});

CPPSPEC_MAIN(quick_log_spec);

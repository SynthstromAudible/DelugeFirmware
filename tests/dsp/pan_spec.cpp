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

// pan_amplitudes: the left and right amplitudes of a pan position.

#include "dsp-next/fixed/vca/pan.h"

#include <cppspec.hpp>

using deluge::dsp::fixed::pan_amplitudes;

// clang-format off
describe pan_spec("pan_amplitudes", ${
	it("leaves both channels at unity in the centre", _ {
		expect(pan_amplitudes(0).left).to_equal(1073741823);
		expect(pan_amplitudes(0).right).to_equal(1073741823);
	});
	it("lowers the far channel part way at a mid pan", _ {
		expect(pan_amplitudes(0x10000000).left).to_equal(805306368);
		expect(pan_amplitudes(0x10000000).right).to_equal(1073741823);
		expect(pan_amplitudes(-0x10000000).left).to_equal(1073741823);
		expect(pan_amplitudes(-0x10000000).right).to_equal(805306368);
	});
	it("silences the far channel at the ends and beyond", _ {
		expect(pan_amplitudes(1 << 30).left).to_equal(0);
		expect(pan_amplitudes(-(1 << 30)).right).to_equal(0);
		expect(pan_amplitudes(0x7FFFFFFF).left).to_equal(0);
	});
});

CPPSPEC_MAIN(pan_spec);

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

#include "midi/device_id.h"

#include <cppspec.hpp>

using namespace deluge::midi;

// clang-format off
describe device_id("a MIDI device's session handle", ${
	it("names none by default", _ {
		expect(static_cast<bool>(DeviceId{})).to_be_false();
	});

	it("names a device once it holds a handle", _ {
		expect(static_cast<bool>(kDin)).to_be_true();
		expect(static_cast<bool>(kUpstreamUsb3)).to_be_true();
	});

	it("gives each fixed port its own handle", _ {
		expect(kDin.value).not_().to_equal(kUpstreamUsb1.value);
		expect(kUpstreamUsb1.value).not_().to_equal(kUpstreamUsb2.value);
		expect(kUpstreamUsb2.value).not_().to_equal(kUpstreamUsb3.value);
	});

	it("numbers the hosted devices after the fixed ports", _ {
		expect(kUpstreamUsb3.value).to_be_less_than(kFirstHosted.value);
	});
});

CPPSPEC_SPEC(device_id)

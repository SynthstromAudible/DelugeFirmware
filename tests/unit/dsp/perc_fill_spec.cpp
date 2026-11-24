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

// What one perc fill tells the time stretcher (synth/sources/sample/perc_fill.h): the chunks it touched, once each,
// and a chunk past the two a fill can touch reported rather than lost silently.

#include "synth/sources/sample/perc_fill.h"

#include <cppspec.hpp>
#include <cstdint>

#include "support/spec_main.h"

namespace {
using deluge::synth::PercChunk;
using deluge::synth::PercFill;
} // namespace

// clang-format off
describe perc_fill_spec_body("deluge::synth::PercFill", ${
	it("notes each chunk once, in the order first touched", _ {
		PercFill fill;
		expect(fill.touch({.reversed = false, .index = 3}) ? 1 : 0).to_equal(1);
		expect(fill.touch({.reversed = false, .index = 3}) ? 1 : 0).to_equal(1);
		expect(fill.touch({.reversed = false, .index = 4}) ? 1 : 0).to_equal(1);
		expect(static_cast<int32_t>(fill.touched_count)).to_equal(2);
		expect(fill.touched[0].index).to_equal(3);
		expect(fill.touched[1].index).to_equal(4);
	});

	it("reports a third chunk it cannot keep", _ {
		PercFill fill;
		(void)fill.touch({.reversed = false, .index = 1});
		(void)fill.touch({.reversed = false, .index = 2});
		expect(fill.touch({.reversed = false, .index = 5}) ? 1 : 0).to_equal(0);
		expect(static_cast<int32_t>(fill.touched_count)).to_equal(2);
	});
});
// clang-format on

DELUGE_SPEC(perc_fill_spec, perc_fill_spec_body)

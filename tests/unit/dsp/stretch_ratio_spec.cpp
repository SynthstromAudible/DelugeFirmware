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

// The stretch ratio that undoes a sample's pitch (synth/sources/sample/stretch_ratio.h): exact within range, and
// saturated, not wrapped, far below the root (decision 3 of docs/dev/time_stretcher_design.md).

#include "synth/sources/sample/stretch_position.h"
#include "synth/sources/sample/stretch_ratio.h"

#include <cppspec.hpp>
#include <cstdint>

#include "support/spec_main.h"

namespace {
using deluge::synth::ratio_undoing_pitch;
} // namespace

// clang-format off
describe stretch_ratio_spec_body("deluge::synth::ratio_undoing_pitch", ${
	it("undoes the pitch exactly within range", _ {
		expect(ratio_undoing_pitch(1 << 24, 1u << 24)).to_equal(1u << 24);
		expect(ratio_undoing_pitch(1 << 24, 1u << 25)).to_equal(1u << 23);
		expect(ratio_undoing_pitch(1 << 24, 65537)).to_equal(4294901760u); // 2^48 / 65537, floored
	});

	it("saturates a ratio past 2^32 instead of wrapping", _ {
		// The legacy's 32-bit truncation gave 0 for both
		expect(ratio_undoing_pitch(1 << 24, 65536)).to_equal(UINT32_MAX);
		expect(ratio_undoing_pitch(1 << 24, 1)).to_equal(UINT32_MAX);
		expect(deluge::synth::combined_increment(deluge::synth::Ratio::from_raw(UINT32_MAX),
		                                         deluge::synth::Increment::from_raw(1)))
		    .to_equal(uint64_t{255});
	});
});
// clang-format on

DELUGE_SPEC(stretch_ratio_spec, stretch_ratio_spec_body)

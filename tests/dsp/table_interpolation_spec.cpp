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

// The decay-curve lookups, getDecay4 and getDecay8, read across a stage of 2^23 positions. Exact values at the
// stage's start, inside its last interval, and at its very end, 2^23, where the input indexes the table's last entry
// with a zero blend: that read must stay inside the 257-entry table (under ASan, a read of the entry past it fails).

#include "dsp-next/fixed/interpolation/table_interpolation.h"

#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::fixed::getDecay4;
using deluge::dsp::fixed::getDecay8;

/// The decay lookups' input width: a stage's 23 position bits.
constexpr uint8_t kStageBits = 23;
/// The whole stage, 2^23: the top of the input range, the table's last entry.
constexpr uint32_t kWholeStage = uint32_t{1} << kStageBits;
/// Half an interval short of the whole stage: the last interval, blended half and half.
constexpr uint32_t kLastIntervalMiddle = kWholeStage - (uint32_t{1} << 14);
} // namespace

// clang-format off
describe decay_lookup("the decay-curve lookups", ${
	it("read the first entry, at Q15, at the stage's start", _ {
		expect(getDecay4(0, kStageBits)).to_equal(65535 * 32768);
		expect(getDecay8(0, kStageBits)).to_equal(65535 * 32768);
	});

	it("blend the last two entries inside the last interval", _ {
		// decayTableSmall4 ends 48, 0 and decayTableSmall8 ends 6, 0: half of each at Q15.
		expect(getDecay4(kLastIntervalMiddle, kStageBits)).to_equal(48 * 16384);
		expect(getDecay8(kLastIntervalMiddle, kStageBits)).to_equal(6 * 16384);
	});

	it("read the last entry alone, inside the table, at the whole stage", _ {
		expect(getDecay4(kWholeStage, kStageBits)).to_equal(0);
		expect(getDecay8(kWholeStage, kStageBits)).to_equal(0);
	});
});
// clang-format on

CPPSPEC_MAIN(decay_lookup);

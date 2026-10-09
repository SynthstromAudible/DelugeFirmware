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
// The bilinear interpolator is pinned on a small table whose entries all differ, against a reference that reads it
// entry by entry, at every row and column interval, on the grid and between it, up to the last interval of each axis.

#include "dsp-next/fixed/interpolation/table_interpolation.h"

#include <cppspec.hpp>
#include <cstdint>
#include <vector>

namespace {
using deluge::dsp::fixed::bilinear_table_interpolator;
using deluge::dsp::fixed::getDecay4;
using deluge::dsp::fixed::getDecay8;

/// 2^2 + 1 rows of 2^3 + 1 entries, every entry distinct and of both signs: a row stride off by one reads another
/// value.
// A built-in array, the shape BilinearTableInterpolator takes.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
constexpr int16_t kGrid[5][9] = {
    {-9000, -7000, -5100, -3000, -1100, 900, 3100, 5000, 7100},
    {-8800, -6900, -4900, -2950, -1000, 1050, 2900, 4950, 6800},
    {-8500, -6600, -4650, -2700, -800, 1200, 3150, 5200, 7000},
    {-8100, -6200, -4300, -2400, -500, 1400, 3300, 5300, 7300},
    {-7700, -5850, -3900, -2000, -150, 1700, 3650, 5550, 7550},
};
/// The interpolator's input widths in this spec: 16 bits on each axis.
constexpr int32_t kAxisBits = 16;

/// kGrid at (@p x, @p y), each 16 bits wide, read entry by entry: each row blended at a Q16 weight along it, the two
/// rows at a Q31 weight across them, with 64-bit products.
int32_t reference_grid(uint32_t x, uint32_t y) {
	const uint32_t column = x >> 13;
	const int64_t along = (x << 3) & 0xFFFFu;
	const uint32_t row = y >> 14;
	const int64_t toward_upper = (int64_t{y} << 17) & 0x7FFF'FFFF;
	const auto row_value = [&](uint32_t r) {
		return kGrid[r][column] * (0x10000 - along) + kGrid[r][column + 1] * along;
	};
	return static_cast<int32_t>((row_value(row) * (0x7FFF'FFFF - toward_upper)) >> 32)
	       + static_cast<int32_t>((row_value(row + 1) * toward_upper) >> 32);
}

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

// clang-format off
describe bilinear_lookup("the bilinear table interpolator", ${
	it("reads the grid's entries at Q16 on the grid", _ {
		expect(bilinear_table_interpolator.interpolateSigned<kGrid>(0, 0, kAxisBits, kAxisBits))
		    .to_equal(-9000 * 65536 / 2);
		expect(bilinear_table_interpolator.interpolateSigned<kGrid>(3u << 13, 2u << 14, kAxisBits, kAxisBits))
		    .to_equal(-2700 * 65536 / 2);
	});
	it("blends as the reference reads the grid, across every interval of both axes", _ {
		std::vector<int32_t> got;
		std::vector<int32_t> reference;
		for (uint32_t y = 0; y < (1u << 16); y += 0x0F01u) {
			for (uint32_t x = 0; x < (1u << 16); x += 0x0301u) {
				got.push_back(bilinear_table_interpolator.interpolateSigned<kGrid>(x, y, kAxisBits, kAxisBits));
				reference.push_back(reference_grid(x, y));
			}
		}
		expect(got.size()).to_equal(std::size_t{18 * 86});
		expect(got).to_equal(reference);
	});
	it("blends the last row and column intervals toward the upper neighbours", _ {
		expect(bilinear_table_interpolator.interpolateSigned<kGrid>(0xFFFFu, 0xFFFFu, kAxisBits, kAxisBits))
		    .to_equal(reference_grid(0xFFFFu, 0xFFFFu));
	});
});
// clang-format on

CPPSPEC_MAIN(decay_lookup, bilinear_lookup);

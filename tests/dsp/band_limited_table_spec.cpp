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

// Behavioural spec for deluge::dsp::fixed::BandlimitedTable::select — the self-selecting band
// pick that replaced the legacy getTableNumber-indexed lookup. Run on x86 (SIMDe) and ARM/QEMU
// via CTest. Pure index logic (no SIMD), but kept dual-arch for parity with the other specs.

#include "dsp-next/fixed/band_limited_table.h"
#include <array>
#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::fixed::Band;
using deluge::dsp::fixed::BandlimitedTable;

// Three bands at magnitude 1 (so each "table" only needs (1<<1)+1 = 3 entries), with ascending
// pitch thresholds; the last is the catch-all.
constexpr std::array<int16_t, 3> kLow{1, 1, 1};
constexpr std::array<int16_t, 3> kMid{2, 2, 2};
constexpr std::array<int16_t, 3> kHigh{3, 3, 3};
constexpr std::array<Band, 3> kBands{{
    {kLow.data(), 1, 1000},
    {kMid.data(), 1, 2000},
    {kHigh.data(), 1, 0xFFFFFFFFu},
}};
const BandlimitedTable kTable{kBands};
} // namespace

// clang-format off
describe band_limited_table("deluge::dsp::fixed BandlimitedTable", ${
	it("selects the first band whose threshold covers the increment", _ {
		expect(kTable.select(0).samples.data()).to_equal(kLow.data());
		expect(kTable.select(500).samples.data()).to_equal(kLow.data());
		expect(kTable.select(1000).samples.data()).to_equal(kLow.data());   // <= is inclusive
		expect(kTable.select(1001).samples.data()).to_equal(kMid.data());
		expect(kTable.select(2000).samples.data()).to_equal(kMid.data());
		expect(kTable.select(2001).samples.data()).to_equal(kHigh.data());
	});

	it("falls back to the last band above every threshold", _ {
		expect(kTable.select(0xFFFFFFFFu).samples.data()).to_equal(kHigh.data());
	});

	it("carries each band's magnitude through", _ {
		expect(kTable.select(500).magnitude).to_equal(1);
	});
});
// clang-format on

CPPSPEC_MAIN(band_limited_table);

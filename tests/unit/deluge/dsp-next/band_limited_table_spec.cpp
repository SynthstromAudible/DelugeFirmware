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
// pick that replaced the legacy get_table_number-indexed lookup. Run on x86 (SIMDe) and ARM/QEMU
// via CTest. Pure index logic (no SIMD), but kept dual-arch for parity with the other specs.

#include "dsp-next/fixed/bandlimited/band_limited_table.h"
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

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

/// The first band whose threshold covers @p inc, walking them all: the pick select() must make.
std::size_t walked(std::span<const Band> bands, uint32_t inc) {
	for (std::size_t b = 0; b < bands.size(); b++) {
		if (inc <= bands[b].max_phase_increment) {
			return b;
		}
	}
	return bands.size() - 1;
}

/// A band set on @p bounds, each band's table a distinct address so a pick can be told apart.
template <std::size_t N>
std::array<Band, N> bands_on(const std::array<uint32_t, N>& bounds, const std::array<int16_t, N + 2>& storage) {
	std::array<Band, N> out{};
	for (std::size_t b = 0; b < N; b++) {
		out[b] = {&storage[b], 1, bounds[b]};
	}
	return out;
}

/// Every increment either side of each bound and of each power of two, then a spread of others.
std::vector<uint32_t> probes(std::span<const uint32_t> bounds) {
	std::vector<uint32_t> out{0, 1, 0xFFFFFFFFu};
	for (uint32_t bound : bounds) {
		out.insert(out.end(), {bound - 1, bound, bound + 1});
	}
	for (uint32_t bit = 0; bit < 32; bit++) {
		out.insert(out.end(), {(uint32_t{1} << bit) - 1, uint32_t{1} << bit, (uint32_t{1} << bit) + 1});
	}
	uint32_t x = 12345;
	for (int i = 0; i < 100000; i++) {
		x = x * 1664525u + 1013904223u;
		out.push_back(x >> (x & 15));
	}
	return out;
}

/// The legacy ladder get_table_number replaced, compare by compare.
std::pair<int32_t, int32_t> ladder(uint32_t inc) {
	constexpr std::array<std::pair<uint32_t, std::pair<int32_t, int32_t>>, 19> kSteps{{
	    {1247086, {0, 13}},    {1764571, {1, 12}},    {2494173, {2, 12}},    {3526245, {3, 11}},
	    {4982560, {4, 11}},    {7040929, {5, 11}},    {9988296, {6, 11}},    {14035840, {7, 11}},
	    {19701684, {8, 11}},   {28256363, {9, 11}},   {40518559, {10, 11}},  {55063683, {11, 11}},
	    {79536431, {12, 11}},  {113025455, {13, 11}}, {165191049, {14, 10}}, {238609294, {15, 10}},
	    {306783378, {16, 10}}, {429496729, {17, 10}}, {715827882, {18, 9}},
	}};
	for (const auto& [bound, result] : kSteps) {
		if (inc <= bound) {
			return result;
		}
	}
	return {19, 9};
}

// The wave kernels' band sets: the analog ladder (all twenty), saw and square's (from band 6), and the triangle's;
// and one on powers of two.
constexpr std::array<uint32_t, 20> kAnalogBounds{
    1247086,  1764571,  2494173,  3526245,   4982560,   7040929,   9988296,   14035840,  19701684,  28256363,
    40518559, 55063683, 79536431, 113025455, 165191049, 238609294, 306783378, 429496729, 715827882, 0xFFFFFFFFu};
constexpr std::array<uint32_t, 14> kSawBounds{9988296,   14035840,  19701684,  28256363,   40518559,
                                              55063683,  79536431,  113025455, 165191049,  238609294,
                                              306783378, 429496729, 715827882, 0xFFFFFFFFu};
// Bounds on powers of two, where an increment of exactly the bound sits at the bottom of its bit width.
constexpr std::array<uint32_t, 4> kPowerBounds{1024, 4096, 65536, 0xFFFFFFFFu};
constexpr std::array<uint32_t, 6> kTriangleBounds{102261126, 143165576, 238609294, 429496729, 715827882, 0xFFFFFFFFu};
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

	it("picks the band walking every band would, on each wave's band set", _ {
		int32_t mismatches = 0;
		const auto check = [&](const auto& bounds) {
			constexpr std::size_t n = std::tuple_size_v<std::remove_cvref_t<decltype(bounds)>>;
			static const std::array<int16_t, n + 2> storage{};
			const auto bands = bands_on(bounds, storage);
			const BandlimitedTable table{bands};
			for (uint32_t inc : probes(bounds)) {
				mismatches += table.select(inc).samples.data() == &storage[walked(bands, inc)] ? 0 : 1;
			}
		};
		check(kAnalogBounds);
		check(kSawBounds);
		check(kTriangleBounds);
		check(kPowerBounds);
		expect(mismatches).to_equal(0);
	});

	it("numbers the bands as the legacy compare ladder did", _ {
		int32_t mismatches = 0;
		for (uint32_t inc : probes(kAnalogBounds)) {
			mismatches += deluge::dsp::fixed::get_table_number(inc) == ladder(inc) ? 0 : 1;
		}
		expect(mismatches).to_equal(0);
	});

	it("carries each band's magnitude through", _ {
		expect(kTable.select(500).magnitude).to_equal(1);
	});
});
// clang-format on

CPPSPEC_MAIN(band_limited_table);

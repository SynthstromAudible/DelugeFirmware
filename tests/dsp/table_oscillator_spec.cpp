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

// Behavioural spec for the deluge::dsp::fixed TableOscillator, run on x86 (SIMDe) and
// ARM/QEMU via CTest (every assertion is an x86-vs-NEON cross-check). Uses small,
// controlled synthetic tables; bit-exact equivalence with the legacy
// waveRenderingFunctionGeneral on the real band-limited tables is the golden harness's job.

#include "dsp-next/core/buffer_view.h"
#include "dsp-next/fixed/osc/table_oscillator.h"
#include <arm_neon.h>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace reference {
using deluge::dsp::Phasor;
using deluge::dsp::UQ;
using deluge::dsp::fixed::Table;

// A raw-Argon reference of TableOscillator (q31 spelt int32_t): the typed oscillator must match it bit for bit.
class TableOscillator {
	const int16_t* samples_;
	int32_t magnitude_;
	Phasor phasor_;

public:
	TableOscillator(Table table, uint32_t phase, uint32_t increment)
	    : samples_{table.samples.data()}, magnitude_{table.magnitude},
	      phasor_{UQ<32>::from_raw(phase), UQ<32>::from_raw(increment)} {}

	Argon<int32_t> render() {
		Argon<uint32_t> value_pair = 0U;
		ArgonHalf<uint16_t> fraction = 0U;

		util::constexpr_for<0, 4, 1>([&]<int i> [[gnu::always_inline]] {
			uint32_t phase = phasor_.advance().raw();
			fraction[i] = phase >> (32 - 16 - magnitude_);
			uint32_t index = phase >> (32 - magnitude_);
			uint32_t pair;
			std::memcpy(&pair, reinterpret_cast<const std::byte*>(samples_) + (index << 1), sizeof(pair));
			value_pair[i] = pair;
		});

		fraction = fraction >> 1;
		ArgonHalf<int16_t> entry = value_pair.Narrow().As<int16_t>();
		ArgonHalf<int16_t> next_entry = value_pair.ShiftRightNarrow<16>().As<int16_t>();
		Argon<int32_t> entry_wide = entry.ShiftLeftLong<16>();
		ArgonHalf<int16_t> slope = next_entry - entry;
		return entry_wide.MultiplyDoubleAddSaturateLong(slope, fraction.As<int16_t>());
	}
};
} // namespace reference

namespace {

/// A phase or increment word as the oscillators take it.
[[nodiscard]] constexpr deluge::dsp::UQ<32> uq(uint32_t word) {
	return deluge::dsp::UQ<32>::from_raw(word);
}
using deluge::dsp::fixed::Table;
using deluge::dsp::fixed::TableOscillator;

// magnitude 2 -> index = phase >> 30, in [0, 4); reads entry + neighbour, so an 8-entry
// table is comfortably in bounds.
constexpr int32_t kMag = 2;
constexpr uint32_t kInc = 0x08000000;

std::uint32_t xorshift(std::uint32_t& s) {
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

/// A random 257-entry table (magnitude 8 and its interpolation neighbour), the int16 extremes in it side by side.
std::array<int16_t, 257> random_table() {
	std::array<int16_t, 257> table{};
	std::uint32_t s = 0x1357'9bdfu;
	for (auto& x : table) {
		x = static_cast<int16_t>(xorshift(s));
	}
	table[10] = INT16_MAX;
	table[11] = INT16_MIN;
	table[12] = INT16_MAX;
	table[200] = INT16_MIN;
	table[201] = INT16_MIN;
	return table;
}

/// Lanes where the oscillator and the reference differ over 2^10 random phases and increments, four blocks each.
std::size_t reference_mismatches() {
	alignas(16) static const auto table = random_table();
	std::size_t bad = 0;
	std::uint32_t s = 0x2468'ace0u;
	for (int k = 0; k < (1 << 10); ++k) {
		const uint32_t phase = xorshift(s);
		const uint32_t increment = xorshift(s) >> (k % 8);
		TableOscillator osc{Table{table, 8}, uq(phase), uq(increment)};
		reference::TableOscillator expected{Table{table, 8}, phase, increment};
		for (int block = 0; block < 4; ++block) {
			const auto got = osc.render().raw().to_array();
			const auto want = expected.render().to_array();
			for (std::size_t i = 0; i < 4; ++i) {
				bad += got[i] != want[i] ? 1 : 0;
			}
		}
	}
	return bad;
}
} // namespace

// clang-format off
describe table_oscillator("deluge::dsp::fixed TableOscillator", ${
	it("matches the raw-Argon reference bit for bit", _ {
		expect(reference_mismatches()).to_equal(std::size_t{0});
	});

	context("a constant table", _ {
		it("renders that constant, promoted to the q31 high half, on every lane", _ {
			// value1 == value2 -> difference 0 -> output is exactly value << 16.
			std::array<int16_t, 8> table{};
			table.fill(0x0123);
			TableOscillator osc{Table{table, kMag}, uq(0u), uq(kInc)};
			int32x4_t out = osc.render().raw();
			expect(vgetq_lane_s32(out, 0)).to_equal(0x01230000);
			expect(vgetq_lane_s32(out, 1)).to_equal(0x01230000);
			expect(vgetq_lane_s32(out, 2)).to_equal(0x01230000);
			expect(vgetq_lane_s32(out, 3)).to_equal(0x01230000);
		});
	});

	context("a rising ramp table", _ {
		it("interpolates within the table's value range", _ {
			std::array<int16_t, 8> table{0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
			TableOscillator osc{Table{table, kMag}, uq(0u), uq(kInc)};
			int32x4_t out = osc.render().raw();
			expect(vgetq_lane_s32(out, 0)).to_be_between(int32_t{0}, int32_t{7000} << 16);
			expect(vgetq_lane_s32(out, 3)).to_be_between(int32_t{0}, int32_t{7000} << 16);
		});
	});

	context("render_block", _ {
		it("fills the buffer with the same samples render() produces", _ {
			std::array<int16_t, 8> table{0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};

			TableOscillator a{Table{table, kMag}, uq(0u), uq(kInc)};
			int32x4_t expected = a.render().raw();

			TableOscillator b{Table{table, kMag}, uq(0u), uq(kInc)};
			std::array<int32_t, 4> buf{};
			b.render_block(deluge::dsp::typed_view(std::span{buf}));

			expect(buf[0]).to_equal(vgetq_lane_s32(expected, 0));
			expect(buf[1]).to_equal(vgetq_lane_s32(expected, 1));
			expect(buf[3]).to_equal(vgetq_lane_s32(expected, 3));
		});
	});
});
// clang-format on

CPPSPEC_MAIN(table_oscillator);

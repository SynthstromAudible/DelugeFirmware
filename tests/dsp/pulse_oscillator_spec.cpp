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

// Behavioural spec for deluge::dsp::fixed::PulseOscillator, run on x86 (SIMDe) and ARM/QEMU
// via CTest (every assertion is therefore an x86-vs-NEON cross-check). Bit-exact equivalence
// with the legacy waveRenderingFunctionPulse on the real square tables is the golden's job.

#include "dsp-next/core/buffer_view.h"
#include "dsp-next/fixed/osc/pulse_oscillator.h"
#include "phase_words.h"
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace reference {
using deluge::dsp::Phasor;
using deluge::dsp::UQ;
using deluge::dsp::fixed::Table;

// A raw-Argon reference of PulseOscillator (q31 spelt int32_t): the typed oscillator must match it bit for bit.
class PulseOscillator {
	const int16_t* samples_;
	int32_t magnitude_;
	Phasor phasor_;
	uint32_t phase_offset_;

	static constexpr int16_t kMin = std::numeric_limits<int16_t>::min();
	static constexpr int16_t kMax = std::numeric_limits<int16_t>::max();

public:
	PulseOscillator(Table table, uint32_t phase, uint32_t increment, uint32_t phase_offset)
	    : samples_{table.samples.data()}, magnitude_{table.magnitude},
	      phasor_{UQ<32>::from_raw(phase), UQ<32>::from_raw(increment)}, phase_offset_{phase_offset} {}

	Argon<int32_t> render() {
		const uint32_t phase = phasor_.phase().raw();
		const uint32_t increment = phasor_.increment().raw();

		Argon<uint32_t> phase_vector = Argon<uint32_t>{phase}.MultiplyAdd(Argon<uint32_t>{1U, 2U, 3U, 4U}, increment);
		Argon<uint32_t> offset_vector = phase_vector + phase_offset_;
		const int32_t fraction_shift = 32 - magnitude_ - 16;
		ArgonHalf<int16_t> fraction_a = (phase_vector >> fraction_shift).Narrow().As<int16_t>();
		ArgonHalf<int16_t> fraction_b = (offset_vector >> fraction_shift).Narrow().As<int16_t>();

		Argon<uint32_t> pair_a{0U};
		Argon<uint32_t> pair_b{0U};
		util::constexpr_for<0, 4, 1>([&]<int i> [[gnu::always_inline]] () {
			uint32_t here = phase + increment * (i + 1);
			uint32_t index_a = here >> (32 - magnitude_);
			uint32_t word_a;
			std::memcpy(&word_a, reinterpret_cast<const std::byte*>(samples_) + (index_a << 1), sizeof(word_a));
			pair_a[i] = word_a;
			uint32_t index_b = (here + phase_offset_) >> (32 - magnitude_);
			uint32_t word_b;
			std::memcpy(&word_b, reinterpret_cast<const std::byte*>(samples_) + (index_b << 1), sizeof(word_b));
			pair_b[i] = word_b;
		});
		ArgonHalf<int16_t> entry_a = pair_a.Narrow().As<int16_t>();
		ArgonHalf<int16_t> next_a = pair_a.ShiftRightNarrow<16>().As<int16_t>();
		ArgonHalf<int16_t> entry_b = pair_b.Narrow().As<int16_t>();
		ArgonHalf<int16_t> next_b = pair_b.ShiftRightNarrow<16>().As<int16_t>();

		ArgonHalf<int16_t> strength_a1 = fraction_a | kMin;
		ArgonHalf<int16_t> strength_a2 = static_cast<int16_t>(kMin) - strength_a1;
		Argon<int32_t> square_a = strength_a2
		                              .MultiplyDoubleSaturateLong(next_a) //<
		                              .MultiplyDoubleAddSaturateLong(strength_a1, entry_a);

		ArgonHalf<int16_t> strength_b2 = fraction_b & kMax;
		ArgonHalf<int16_t> strength_b1 = static_cast<int16_t>(kMax) - strength_b2;
		Argon<int32_t> square_b = strength_b2
		                              .MultiplyDoubleSaturateLong(next_b) //<
		                              .MultiplyDoubleAddSaturateLong(strength_b1, entry_b);

		Argon<int32_t> output = square_a.MultiplyRoundFixedQMax(square_b) << 1;
		phasor_.seek(UQ<32>::from_raw(phase + increment * 4));
		return output;
	}
};
} // namespace reference

namespace {
using deluge::dsp::fixed::PulseOscillator;
using deluge::dsp::fixed::Table;

constexpr int32_t kMag = 3;           // index = phase >> 29, in [0, 8)
constexpr uint32_t kInc = 0x08000000; // phase increment
// A bipolar square over one cycle (8 cells + a wrap neighbour); the pulse kernel ring-mods
// two copies of it, so it needs real square-like (large, bipolar) values to be non-degenerate.
constexpr std::array<int16_t, 9> kTable{-30000, -30000, -30000, -30000, 30000, 30000, 30000, 30000, -30000};

// Scalar reference for the pulse math: each square interpolated between its table entry and the next by the 15 phase
// bits just below the index, A with flipped polarity, then ring-modulated. The test table stays below saturation, so
// the NEON multiply/add operations reduce to ordinary arithmetic.
int32_t referencePulse(uint32_t phase, uint32_t offset, const int16_t* table, int magnitude) {
	auto interpolate = [&](uint32_t position, bool inverted) -> int64_t {
		uint32_t index = position >> (32 - magnitude);
		int32_t fraction = (position >> (32 - magnitude - 16)) & 0x7FFF;
		if (inverted) {
			return 2LL * ((fraction - 32768LL) * table[index] - int64_t(fraction) * table[index + 1]);
		}
		return 2LL * ((32767LL - fraction) * table[index] + int64_t(fraction) * table[index + 1]);
	};
	int64_t product = interpolate(phase, true) * interpolate(phase + offset, false);
	return static_cast<int32_t>(((product + (1LL << 30)) >> 31) * 2);
}

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

/// Lanes where the oscillator and the reference differ over 2^10 random phases and increments, at phase offsets 0,
/// a half cycle and random, four blocks each.
std::size_t reference_mismatches() {
	alignas(16) static const auto table = random_table();
	std::size_t bad = 0;
	std::uint32_t s = 0x8642'0bdfu;
	for (int k = 0; k < (1 << 10); ++k) {
		const uint32_t phase = xorshift(s);
		const uint32_t increment = xorshift(s) >> (k % 8);
		const uint32_t random_offset = xorshift(s);
		for (const uint32_t offset : {0U, 0x8000'0000U, random_offset}) {
			PulseOscillator osc{Table{table, 8}, uq(phase), uq(increment), uq(offset)};
			reference::PulseOscillator expected{Table{table, 8}, phase, increment, offset};
			for (int block = 0; block < 4; ++block) {
				const auto got = osc.render().raw().to_array();
				const auto want = expected.render().to_array();
				for (std::size_t i = 0; i < 4; ++i) {
					bad += got[i] != want[i] ? 1 : 0;
				}
			}
		}
	}
	// Square A's weights saturate only where its fraction's low 15 bits are 0 (w − 1 = −1, so a1 = −32768): put every
	// lane there, on the table's adjacent INT16_MIN entries (index 200), with and without the fraction's top bit.
	for (const uint32_t phase : {200U << 24, (200U << 24) | 0x0080'0000U}) {
		for (const uint32_t offset : {0U, 0x8000'0000U}) {
			PulseOscillator osc{Table{table, 8}, uq(phase), uq(0), uq(offset)};
			reference::PulseOscillator expected{Table{table, 8}, phase, 0, offset};
			const auto got = osc.render().raw().to_array();
			const auto want = expected.render().to_array();
			for (std::size_t i = 0; i < 4; ++i) {
				bad += got[i] != want[i] ? 1 : 0;
			}
		}
	}
	return bad;
}

std::array<int32_t, 4> renderOnce(uint32_t offset) {
	PulseOscillator osc{Table{kTable, kMag}, uq(0u), uq(kInc), uq(offset)};
	std::array<int32_t, 4> out{};
	osc.render().raw().StoreTo(out.data());
	return out;
}
} // namespace

// clang-format off
describe pulse_oscillator("deluge::dsp::fixed PulseOscillator", ${
	it("matches the raw-Argon reference bit for bit", _ {
		expect(reference_mismatches()).to_equal(std::size_t{0});
	});
	context("render_block", _ {
		it("fills the buffer with the same samples render() produces", _ {
			PulseOscillator a{Table{kTable, kMag}, uq(0u), uq(kInc), uq(0x60000000u)};
			std::array<int32_t, 4> expected{};
			a.render().raw().StoreTo(expected.data());

			PulseOscillator b{Table{kTable, kMag}, uq(0u), uq(kInc), uq(0x60000000u)};
			std::array<int32_t, 4> buf{};
			b.render_block(deluge::dsp::typed_view(std::span{buf}));

			expect(buf).to_equal(expected);
		});
	});

	context("interpolation", _ {
		it("interpolates between table entries across table sizes and phase wraparound", _ {
			for (int32_t magnitude = 6; magnitude <= 13; ++magnitude) {
				std::vector<int16_t> table((1U << magnitude) + 1);
				for (size_t i = 0; i + 1 < table.size(); ++i) {
					table[i] = static_cast<int16_t>(static_cast<int32_t>((i * 7919U) % 20001U) - 10000);
				}
				table.back() = table.front();
				for (uint32_t phase : {0U, 0x12345678U, 0xFFFF1234U}) {
					for (uint32_t increment : {0x12345U, 0x1020304U}) {
						for (uint32_t offset : {0U, 0x13579BDFU, 0xF0001234U}) {
							PulseOscillator osc{Table{table, magnitude}, uq(phase), uq(increment), uq(offset)};
							std::array<int32_t, 4> actual{};
							osc.render().raw().StoreTo(actual.data());
							for (uint32_t lane = 0; lane < 4; ++lane) {
								expect(actual[lane]).to_equal(
								    referencePulse(phase + increment * (lane + 1), offset, table.data(), magnitude));
							}
							expect(osc.phase().raw()).to_equal(phase + increment * 4);
						}
					}
				}
			}
		});
	});

	context("duty cycle", _ {
		it("produces different output for different pulse widths", _ {
			// The phase offset is the duty cycle: different offsets must give different waves.
			expect(renderOnce(0x40000000u) != renderOnce(0xC0000000u)).to_be_truthy();
		});

		it("is deterministic for a given width", _ {
			expect(renderOnce(0x40000000u)).to_equal(renderOnce(0x40000000u));
		});
	});
});
// clang-format on

CPPSPEC_MAIN(pulse_oscillator);

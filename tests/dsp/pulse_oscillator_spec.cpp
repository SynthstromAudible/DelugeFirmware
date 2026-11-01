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

#include "dsp-next/fixed/osc/pulse_oscillator.h"
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

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

std::array<int32_t, 4> renderOnce(uint32_t offset) {
	PulseOscillator osc{Table{kTable, kMag}, 0u, kInc, offset};
	std::array<int32_t, 4> out{};
	osc.render().StoreTo(out.data());
	return out;
}
} // namespace

// clang-format off
describe pulse_oscillator("deluge::dsp::fixed PulseOscillator", ${
	context("render_block", _ {
		it("fills the buffer with the same samples render() produces", _ {
			PulseOscillator a{Table{kTable, kMag}, 0u, kInc, 0x60000000u};
			std::array<int32_t, 4> expected{};
			a.render().StoreTo(expected.data());

			PulseOscillator b{Table{kTable, kMag}, 0u, kInc, 0x60000000u};
			std::array<int32_t, 4> buf{};
			b.render_block(deluge::dsp::fixed::Buffer{buf});

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
							PulseOscillator osc{Table{table, magnitude}, phase, increment, offset};
							std::array<int32_t, 4> actual{};
							osc.render().StoreTo(actual.data());
							for (uint32_t lane = 0; lane < 4; ++lane) {
								expect(actual[lane]).to_equal(
								    referencePulse(phase + increment * (lane + 1), offset, table.data(), magnitude));
							}
							expect(osc.phase()).to_equal(phase + increment * 4);
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

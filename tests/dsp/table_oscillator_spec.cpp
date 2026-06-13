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

#include "dsp-next/fixed/table_oscillator.h"
#include <arm_neon.h>
#include <array>
#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::fixed::Table;
using deluge::dsp::fixed::TableOscillator;

// magnitude 2 -> index = phase >> 30, in [0, 4); reads entry + neighbour, so an 8-entry
// table is comfortably in bounds.
constexpr int32_t kMag = 2;
constexpr uint32_t kInc = 0x08000000;
} // namespace

// clang-format off
describe table_oscillator("deluge::dsp::fixed TableOscillator", ${
	context("a constant table", _ {
		it("renders that constant, promoted to the q31 high half, on every lane", _ {
			// value1 == value2 -> difference 0 -> output is exactly value << 16.
			std::array<int16_t, 8> table{};
			table.fill(0x0123);
			TableOscillator osc{Table{table, kMag}, 0u, kInc};
			int32x4_t out = osc.render();
			expect(vgetq_lane_s32(out, 0)).to_equal(0x01230000);
			expect(vgetq_lane_s32(out, 1)).to_equal(0x01230000);
			expect(vgetq_lane_s32(out, 2)).to_equal(0x01230000);
			expect(vgetq_lane_s32(out, 3)).to_equal(0x01230000);
		});
	});

	context("a rising ramp table", _ {
		it("interpolates within the table's value range", _ {
			std::array<int16_t, 8> table{0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
			TableOscillator osc{Table{table, kMag}, 0u, kInc};
			int32x4_t out = osc.render();
			expect(vgetq_lane_s32(out, 0)).to_be_between(int32_t{0}, int32_t{7000} << 16);
			expect(vgetq_lane_s32(out, 3)).to_be_between(int32_t{0}, int32_t{7000} << 16);
		});
	});

	context("renderBlock", _ {
		it("fills the buffer with the same samples render() produces", _ {
			std::array<int16_t, 8> table{0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};

			TableOscillator a{Table{table, kMag}, 0u, kInc};
			int32x4_t expected = a.render();

			TableOscillator b{Table{table, kMag}, 0u, kInc};
			std::array<int32_t, 4> buf{};
			b.renderBlock(deluge::dsp::fixed::Buffer{buf});

			expect(buf[0]).to_equal(vgetq_lane_s32(expected, 0));
			expect(buf[1]).to_equal(vgetq_lane_s32(expected, 1));
			expect(buf[3]).to_equal(vgetq_lane_s32(expected, 3));
		});
	});
});
// clang-format on

CPPSPEC_MAIN(table_oscillator);

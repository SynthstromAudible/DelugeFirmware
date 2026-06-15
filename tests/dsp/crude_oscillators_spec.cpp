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

// Behavioural spec for the deluge::dsp::fixed crude (non-band-limited) oscillators and their
// VCA, run on x86 (SIMDe) and ARM/QEMU via CTest. Bit-exact equivalence with the legacy crude
// loops on real presets is the golden harness's job; here we pin the per-sample kernels.

#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/bandlimited/waves.h" // square, triangle
#include "dsp-next/fixed/osc/crude_oscillators.h"
#include "dsp-next/fixed/osc/sine_table.h" // sine_table
#include "dsp-next/fixed/vca/crude_amplitude.h"
#include "util/fixedpoint.h" // multiply_accumulate_32x32_rshift32_rounded
#include <array>
#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::Phasor;
using deluge::dsp::fixed::CrudeAmplitude;
using deluge::dsp::fixed::CrudeSaw;
using deluge::dsp::fixed::CrudeSine;
using deluge::dsp::fixed::CrudeSquare;
using deluge::dsp::fixed::CrudeTriangle;
using deluge::dsp::fixed::sine_table;
using Buffer = deluge::dsp::fixed::Buffer;
using Signal = deluge::dsp::fixed::Signal;
} // namespace

// clang-format off
describe crude_oscillators("deluge::dsp::fixed crude oscillators", ${
	context("CrudeSaw", _ {
		it("emits the phase ramp itself", _ {
			CrudeSaw osc{Phasor{0u, 0x10000000u}};
			std::array<int32_t, 4> out{};
			osc.renderBlock(Buffer{out});
			// Pre-increment: sample i is phase + (i+1) * increment.
			expect(out[0]).to_equal(static_cast<int32_t>(0x10000000));
			expect(out[1]).to_equal(static_cast<int32_t>(0x20000000));
			expect(out[2]).to_equal(static_cast<int32_t>(0x30000000));
			expect(out[3]).to_equal(static_cast<int32_t>(0x40000000));
		});
	});

	context("CrudeSquare", _ {
		it("switches at the pulse-width threshold", _ {
			CrudeSquare osc{Phasor{0u, 0x20000000u}, 0x80000000u}; // 50% duty
			std::array<int32_t, 4> out{};
			osc.renderBlock(Buffer{out});
			// phases 0x20/0x40/0x60M are below the threshold (high), 0x80M is at/above it (low).
			expect(out[0]).to_equal(deluge::dsp::fixed::square(0x20000000u, 0x80000000u));
			expect(out[2]).to_equal(deluge::dsp::fixed::square(0x60000000u, 0x80000000u));
			expect(out[3]).to_equal(deluge::dsp::fixed::square(0x80000000u, 0x80000000u));
			expect(out[0] > 0 && out[3] < 0).to_be_truthy();
		});
	});

	context("CrudeTriangle", _ {
		it("matches the triangle kernel at each phase", _ {
			CrudeTriangle osc{Phasor{0u, 0x30000000u}};
			std::array<int32_t, 4> out{};
			osc.renderBlock(Buffer{out});
			expect(out[0]).to_equal(deluge::dsp::fixed::triangle(0x30000000u));
			expect(out[1]).to_equal(deluge::dsp::fixed::triangle(0x60000000u));
			expect(out[2]).to_equal(deluge::dsp::fixed::triangle(0x90000000u));
			expect(out[3]).to_equal(deluge::dsp::fixed::triangle(0xC0000000u));
		});
	});

	context("CrudeSine", _ {
		it("matches the sine LUT at each phase", _ {
			CrudeSine osc{Phasor{0u, 0x30000000u}};
			std::array<int32_t, 4> out{};
			osc.renderBlock(Buffer{out});
			expect(out[0]).to_equal(sine_table.value(0x30000000u));
			expect(out[1]).to_equal(sine_table.value(0x60000000u));
			expect(out[2]).to_equal(sine_table.value(0x90000000u));
			expect(out[3]).to_equal(sine_table.value(0xC0000000u));
		});
	});

	context("CrudeAmplitude", _ {
		it("ramps the amplitude per sample and rounded-MACs raw into mix", _ {
			std::array<int32_t, 4> raw{100000000, -50000000, 2000000000, -2000000000};
			std::array<int32_t, 4> mix{7, -7, 123, -123};
			std::array<int32_t, 4> out{};
			constexpr int32_t kAmp = 0x20000000;
			constexpr int32_t kInc = 0x00100000;
			CrudeAmplitude vca{kAmp, kInc};
			vca.renderBlock(Signal{raw}, Signal{mix}, Buffer{out});
			for (int i = 0; i < 4; ++i) {
				// Pre-increment: sample i uses amplitude kAmp + (i+1) * kInc.
				int32_t expected = multiply_accumulate_32x32_rshift32_rounded(mix[i], raw[i], kAmp + (i + 1) * kInc);
				expect(out[i]).to_equal(expected);
			}
		});
	});
});
// clang-format on

CPPSPEC_MAIN(crude_oscillators);

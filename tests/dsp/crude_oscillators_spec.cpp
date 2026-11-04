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

#include "dsp-next/core/buffer_view.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/core/q31_ops.h"            // multiply_accumulate_32x32_rshift32_rounded
#include "dsp-next/fixed/bandlimited/waves.h" // square, triangle_half_scale
#include "dsp-next/fixed/osc/crude_oscillators.h"
#include "dsp-next/fixed/osc/sine_table.h" // kSineTable
#include "dsp-next/fixed/vca/crude_amplitude.h"
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>

namespace {
using deluge::dsp::Phasor;
using deluge::dsp::Q;
using deluge::dsp::typed_view;
using deluge::dsp::UQ;
using deluge::dsp::fixed::CrudeAmplitude;
using deluge::dsp::fixed::CrudeSaw;
using deluge::dsp::fixed::CrudeSine;
using deluge::dsp::fixed::CrudeSquare;
using deluge::dsp::fixed::CrudeTriangle;
using deluge::dsp::fixed::kSineTable;
using Buffer = deluge::dsp::fixed::Buffer;
using Signal = deluge::dsp::fixed::Signal;

/// CrudeAmplitude::render_block's loop as it was in raw q31, verbatim.
void reference_crude_amplitude(int32_t amplitude_, int32_t increment_, Signal input_a, Signal input_b, Buffer output) {
	for (std::size_t i = 0; i < output.size(); ++i) {
		amplitude_ += increment_;
		output[i] = multiply_accumulate_32x32_rshift32_rounded(input_b[i], input_a[i], amplitude_);
	}
}

uint32_t xorshift(uint32_t& x) {
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}
} // namespace

// clang-format off
describe crude_oscillators("deluge::dsp::fixed crude oscillators", ${
	context("CrudeSaw", _ {
		it("emits the phase ramp itself", _ {
			CrudeSaw osc{Phasor{UQ<32>::from_raw(0u), UQ<32>::from_raw(0x10000000u)}};
			std::array<int32_t, 4> out{};
			osc.render_block(Buffer{out});
			// Pre-increment: sample i is phase + (i+1) * increment.
			expect(out[0]).to_equal(static_cast<int32_t>(0x10000000));
			expect(out[1]).to_equal(static_cast<int32_t>(0x20000000));
			expect(out[2]).to_equal(static_cast<int32_t>(0x30000000));
			expect(out[3]).to_equal(static_cast<int32_t>(0x40000000));
		});
	});

	context("CrudeSquare", _ {
		it("switches at the pulse-width threshold", _ {
			CrudeSquare osc{Phasor{UQ<32>::from_raw(0u), UQ<32>::from_raw(0x20000000u)}, 0x80000000u}; // 50% duty
			std::array<int32_t, 4> out{};
			osc.render_block(Buffer{out});
			// phases 0x20/0x40/0x60M are below the threshold (high), 0x80M is at/above it (low).
			expect(out[0]).to_equal(deluge::dsp::fixed::square(0x20000000u, 0x80000000u));
			expect(out[2]).to_equal(deluge::dsp::fixed::square(0x60000000u, 0x80000000u));
			expect(out[3]).to_equal(deluge::dsp::fixed::square(0x80000000u, 0x80000000u));
			expect(out[0] > 0 && out[3] < 0).to_be_truthy();
		});
	});

	context("CrudeTriangle", _ {
		it("matches the triangle kernel at each phase", _ {
			CrudeTriangle osc{Phasor{UQ<32>::from_raw(0u), UQ<32>::from_raw(0x30000000u)}};
			std::array<int32_t, 4> out{};
			osc.render_block(Buffer{out});
			expect(out[0]).to_equal(deluge::dsp::fixed::triangle_half_scale(0x30000000u));
			expect(out[1]).to_equal(deluge::dsp::fixed::triangle_half_scale(0x60000000u));
			expect(out[2]).to_equal(deluge::dsp::fixed::triangle_half_scale(0x90000000u));
			expect(out[3]).to_equal(deluge::dsp::fixed::triangle_half_scale(0xC0000000u));
		});
	});

	context("CrudeSine", _ {
		it("matches the sine LUT at each phase", _ {
			CrudeSine osc{Phasor{UQ<32>::from_raw(0u), UQ<32>::from_raw(0x30000000u)}};
			std::array<int32_t, 4> out{};
			osc.render_block(Buffer{out});
			expect(out[0]).to_equal(kSineTable.value(0x30000000u));
			expect(out[1]).to_equal(kSineTable.value(0x60000000u));
			expect(out[2]).to_equal(kSineTable.value(0x90000000u));
			expect(out[3]).to_equal(kSineTable.value(0xC0000000u));
		});
	});

	context("CrudeAmplitude", _ {
		it("ramps the amplitude per sample and rounded-MACs raw into mix", _ {
			std::array<int32_t, 4> raw{100000000, -50000000, 2000000000, -2000000000};
			std::array<int32_t, 4> mix{7, -7, 123, -123};
			std::array<int32_t, 4> out{};
			constexpr int32_t kAmp = 0x20000000;
			constexpr int32_t kInc = 0x00100000;
			CrudeAmplitude vca{Q<31>::from_raw(kAmp), Q<31>::from_raw(kInc)};
			vca.render_block(typed_view(Signal{raw}), typed_view(Signal{mix}), typed_view(Buffer{out}));
			for (int i = 0; i < 4; ++i) {
				// Pre-increment: sample i uses amplitude kAmp + (i+1) * kInc.
				int32_t expected = multiply_accumulate_32x32_rshift32_rounded(mix[i], raw[i], kAmp + (i + 1) * kInc);
				expect(out[i]).to_equal(expected);
			}
		});
		it("mixes as the raw reference does, over random blocks and ramps", _ {
			int32_t bad = 0;
			uint32_t x = 0x9E3779B9u;
			for (int block = 0; block < 2048; ++block) {
				std::array<int32_t, 64> raw{};
				std::array<int32_t, 64> mix{};
				for (std::size_t i = 0; i < raw.size(); ++i) {
					raw[i] = static_cast<int32_t>(xorshift(x));
					mix[i] = static_cast<int32_t>(xorshift(x));
				}
				// Steady and ramping amplitudes within ±2^30, stepping at most 2^24 a sample: the raw reference's
				// signed add must not overflow
				const int32_t amplitude = static_cast<int32_t>(xorshift(x)) >> 1;
				const int32_t increment = block % 4 == 0 ? 0 : static_cast<int32_t>(xorshift(x)) >> (7 + (block % 17));
				std::array<int32_t, 64> live{};
				std::array<int32_t, 64> ref{};
				CrudeAmplitude{Q<31>::from_raw(amplitude), Q<31>::from_raw(increment)}.render_block(
				    typed_view(Signal{raw}), typed_view(Signal{mix}), typed_view(Buffer{live}));
				reference_crude_amplitude(amplitude, increment, Signal{raw}, Signal{mix}, Buffer{ref});
				bad += live != ref ? 1 : 0;
			}
			expect(bad).to_equal(0);
		});
	});
});
// clang-format on

CPPSPEC_MAIN(crude_oscillators);

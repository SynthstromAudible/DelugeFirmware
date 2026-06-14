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

// Pins deluge::dsp::fixed::windowed_sinc_apply (the argon windowed-sinc apply) bit-for-bit against
// the legacy hand-written NEON it replaces, over edge cases + many deterministic-random inputs. This
// is the gate for argon-ifying the wavetable render: the argon form must match the raw NEON exactly,
// on both x86 (SIMDe) and ARM/QEMU. (The raw-NEON reference here is the verbatim apply from the
// legacy WaveTable::doRenderingLoop.)

#include "dsp-next/fixed/sinc_interpolation.h"
#include <arm_neon.h>
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <random>

namespace {
using deluge::dsp::fixed::SincWindow;
using deluge::dsp::fixed::windowed_sinc_apply;

// kernel_row spans two adjacent 16-tap rows (32 int16); samples is one 16-tap window.
constexpr int32_t kKernelTaps = 32;
constexpr int32_t kWindow = 16;

// The verbatim legacy NEON apply (from WaveTable::doRenderingLoop): blend two kernel rows by
// row_blend, multiply-accumulate-long against the samples, horizontal-add.
int32_t reference_apply(const int16_t* kernel_row, int16_t row_blend, const int16_t* samples) {
	int16x8_t kernelVector[2];
	for (int32_t i = 0; i < 2; i++) {
		int16x8_t value1 = vld1q_s16(kernel_row + (i << 3));
		int16x8_t value2 = vld1q_s16(kernel_row + 16 + (i << 3));
		int16x8_t difference = vsubq_s16(value2, value1);
		int16x8_t multipliedDifference = vqdmulhq_n_s16(difference, row_blend);
		kernelVector[i] = vaddq_s16(value1, multipliedDifference);
	}
	int32x4_t multiplied;
	for (int32_t i = 0; i < 2; i++) {
		int16x8_t s = vld1q_s16(samples + (i << 3));
		if (i == 0) {
			multiplied = vmull_s16(vget_low_s16(kernelVector[i]), vget_low_s16(s));
		}
		else {
			multiplied = vmlal_s16(multiplied, vget_low_s16(kernelVector[i]), vget_low_s16(s));
		}
		multiplied = vmlal_s16(multiplied, vget_high_s16(kernelVector[i]), vget_high_s16(s));
	}
	int32x2_t twosies = vadd_s32(vget_high_s32(multiplied), vget_low_s32(multiplied));
	int32x2_t onesie = vpadd_s32(twosies, twosies);
	return vget_lane_s32(onesie, 0);
}

int32_t argon_apply(const int16_t* kernel_row, int16_t row_blend, const int16_t* samples) {
	SincWindow window{Argon<int16_t>::Load(samples), Argon<int16_t>::Load(samples + Argon<int16_t>::lanes)};
	return windowed_sinc_apply(kernel_row, row_blend, window);
}
} // namespace

// clang-format off
describe sinc_interpolation("deluge::dsp::fixed windowed_sinc_apply", ${
	it("matches the raw-NEON apply at the kernel-blend extremes", _ {
		std::array<int16_t, kKernelTaps> kernel{};
		std::array<int16_t, kWindow> samples{};
		for (int32_t i = 0; i < kKernelTaps; i++) { kernel[i] = static_cast<int16_t>(1000 - 137 * i); }
		for (int32_t i = 0; i < kWindow; i++) { samples[i] = static_cast<int16_t>(-2000 + 311 * i); }
		for (int16_t row_blend : {int16_t{0}, int16_t{1}, int16_t{0x4000}, int16_t{0x7FFF}}) {
			expect(argon_apply(kernel.data(), row_blend, samples.data()))
			    .to_equal(reference_apply(kernel.data(), row_blend, samples.data()));
		}
	});

	it("matches the raw-NEON apply at int16 saturation extremes", _ {
		std::array<int16_t, kKernelTaps> kernel{};
		std::array<int16_t, kWindow> samples{};
		kernel.fill(INT16_MIN);
		samples.fill(INT16_MIN); // exercises vqdmulhq saturation + the long MAC sign
		expect(argon_apply(kernel.data(), 0x7FFF, samples.data()))
		    .to_equal(reference_apply(kernel.data(), 0x7FFF, samples.data()));
		kernel.fill(INT16_MAX);
		samples.fill(INT16_MAX);
		expect(argon_apply(kernel.data(), 0x7FFF, samples.data()))
		    .to_equal(reference_apply(kernel.data(), 0x7FFF, samples.data()));
	});

	it("matches the raw-NEON apply over many random inputs", _ {
		std::mt19937 rng(0xC0FFEEu); // fixed seed: x86 and ARM draw the same inputs
		std::uniform_int_distribution<int32_t> sample(INT16_MIN, INT16_MAX);
		std::uniform_int_distribution<int32_t> fraction(0, 0x7FFF);
		for (int32_t trial = 0; trial < 4000; trial++) {
			std::array<int16_t, kKernelTaps> kernel{};
			std::array<int16_t, kWindow> samples{};
			for (int16_t& k : kernel) { k = static_cast<int16_t>(sample(rng)); }
			for (int16_t& s : samples) { s = static_cast<int16_t>(sample(rng)); }
			int16_t row_blend = static_cast<int16_t>(fraction(rng));
			expect(argon_apply(kernel.data(), row_blend, samples.data()))
			    .to_equal(reference_apply(kernel.data(), row_blend, samples.data()));
		}
	});
});
// clang-format on

CPPSPEC_MAIN(sinc_interpolation);

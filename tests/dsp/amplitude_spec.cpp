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

// Behavioural spec for the deluge::dsp::fixed Amplitude VCA mixer (out = raw*amp + mix).
// Runs on BOTH x86 (SIMDe) and ARM/QEMU via CTest, so every assertion is also an
// x86-vs-NEON cross-check. (Bit-exact equivalence with the legacy
// applyAmplitudeVectorToBuffer is covered separately by the golden harness.)

#include "dsp-next/fixed/amplitude.h"
#include <arm_neon.h>
#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::fixed::Amplitude;

constexpr int32_t kOneQ31 = 0x7FFFFFFF;

// Apply a fresh VCA to one broadcast 4-lane vector: out = (raw * amp) + mix.
int32x4_t applyOnce(int32_t amplitude, int32_t increment, int32_t raw, int32_t mix) {
	Amplitude vca{amplitude, increment};
	return vca.render(Argon<int32_t>{raw}, Argon<int32_t>{mix});
}
} // namespace

// clang-format off
describe amplitude_vca("deluge::dsp::fixed Amplitude (VCA mixer)", ${
	context("amplitude scaling", _ {
		it("passes the mix through unchanged at zero amplitude", _ {
			int32x4_t out = applyOnce(0, 0, 0x40000000, 0x12345678);
			expect(vgetq_lane_s32(out, 0)).to_equal(0x12345678);
			expect(vgetq_lane_s32(out, 3)).to_equal(0x12345678);
		});
		it("produces a positive, attenuated output for a positive input", _ {
			// createAmplitudeVector halves the amplitude, so full scale maps below the input.
			int32_t out0 = vgetq_lane_s32(applyOnce(kOneQ31, 0, 0x40000000, 0), 0);
			expect(out0).to_be_greater_than(0);
			expect(out0).to_be_less_than(0x40000000);
		});
		it("scales monotonically with amplitude", _ {
			int32_t quarter = vgetq_lane_s32(applyOnce(kOneQ31 / 4, 0, kOneQ31, 0), 0);
			int32_t full = vgetq_lane_s32(applyOnce(kOneQ31, 0, kOneQ31, 0), 0);
			expect(full).to_be_greater_than(quarter);
		});
	});

	context("mix accumulation", _ {
		it("adds exactly the existing mix on top of the scaled wave", _ {
			// out(raw, mix) - out(raw, 0) == mix, for the same amplitude (scaled term identical).
			constexpr int32_t mix = 0x0ABCDEF0;
			int32x4_t with_mix = applyOnce(kOneQ31, 0, 0x40000000, mix);
			int32x4_t no_mix = applyOnce(kOneQ31, 0, 0x40000000, 0);
			expect(vgetq_lane_s32(with_mix, 0) - vgetq_lane_s32(no_mix, 0)).to_equal(mix);
			expect(vgetq_lane_s32(with_mix, 2) - vgetq_lane_s32(no_mix, 2)).to_equal(mix);
		});
	});

	context("the per-lane amplitude ramp", _ {
		it("rises across the four lanes with a positive increment", _ {
			int32x4_t out = applyOnce(0, 0x08000000, kOneQ31, 0);
			expect(vgetq_lane_s32(out, 0)).to_be_less_than(vgetq_lane_s32(out, 1));
			expect(vgetq_lane_s32(out, 1)).to_be_less_than(vgetq_lane_s32(out, 2));
			expect(vgetq_lane_s32(out, 2)).to_be_less_than(vgetq_lane_s32(out, 3));
		});
	});
});
// clang-format on

CPPSPEC_MAIN(amplitude_vca);

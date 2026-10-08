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
// x86-vs-NEON cross-check. Bit-exact equivalence with the legacy
// applyAmplitudeVectorToBuffer is covered by the golden harness for whole vectors, and by the
// "blocks that are not a lane multiple" cases here, whose values were recorded from the legacy
// function (which rounds the block up to whole vectors) at -m32 and -m64.

#include "dsp-next/fixed/vca/amplitude.h"
#include <arm_neon.h>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {
using deluge::dsp::fixed::Amplitude;

constexpr int32_t kOneQ31 = 0x7FFFFFFF;

// Apply a fresh VCA to one broadcast 4-lane vector: out = (raw * amp) + mix.
int32x4_t applyOnce(int32_t amplitude, int32_t increment, int32_t raw, int32_t mix) {
	Amplitude vca{amplitude, increment};
	return vca.render(Argon<int32_t>{raw}, Argon<int32_t>{mix});
}

constexpr std::array<int32_t, 7> kRaw{0x7FFFFFFF, -0x40000000, 0x12345678, -0x7FFFFFFF,
                                      0x3C3C3C3C, -0x0F0F0F0F, 0x60000000};
constexpr std::array<int32_t, 7> kMix{0x01000000,  -0x02000000, 0x00ABCDEF, 0x10000000,
                                      -0x00123456, 0x05555555,  -0x20000000};

// A fresh VCA over the first n samples, mixed in place into the mix (as the voice calls it).
std::vector<int32_t> mixBlock(int32_t amplitude, int32_t increment, std::size_t n) {
	std::vector<int32_t> out(kMix.begin(), kMix.begin() + static_cast<std::ptrdiff_t>(n));
	Amplitude vca{amplitude, increment};
	vca.renderBlock(std::span{kRaw}.first(n), out, out);
	return out;
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

	context("blocks that are not a lane multiple", _ {
		it("ramps across a 5-sample block, its tail included", _ {
			expect(mixBlock(0x20000000, 0x00800000, 5))
			    .to_equal(std::vector<int32_t>{289406975, -171966464, 51226431, -16777216, 134998472});
		});
		it("ramps down across a 7-sample block, its tail included", _ {
			expect(mixBlock(0x50000000, -0x00C00000, 7))
			    .to_equal(std::vector<int32_t>{681574399, -362807296, 104018737, -377487360, 299809946, 14967908,
			                                   -66584576});
		});
		it("matches a fresh VCA per block, each block starting where the last one's ramp ended", _ {
			expect(mixBlock(0x30000000, 0x00400000, 5))
			    .to_equal(std::vector<int32_t>{421527551, -236978176, 69420390, -142606336, 193225280});
			expect(mixBlock(0x30000000 + (5 * 0x00400000), 0x00400000, 7))
			    .to_equal(std::vector<int32_t>{432013311, -242221056, 70911698, -153092096, 198159755, 39393560,
			                                   -216006656});
		});
	});
});
// clang-format on

CPPSPEC_MAIN(amplitude_vca);

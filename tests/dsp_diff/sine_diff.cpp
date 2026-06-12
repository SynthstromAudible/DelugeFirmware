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

/// Differential test of the vectorised FM sine kernels — SineOsc::getSineVector /
/// doFMVector (argon) and the vqdmulh/vqrdmulh amplitude ops used around them in
/// Voice::renderSineWaveWithFeedback / renderFMWithFeedbackAdd. Built two ways
/// (ARM/NEON under qemu vs x86 SIMDe portable); outputs must match bit-exactly.
/// Hunting ground for the fm_2op golden-case x86/ARM divergence.

#include "dsp/oscillators/sine_osc.h"

#include <arm_neon.h>
#include <cstdint>
#include <cstdio>

using deluge::dsp::SineOsc;

int main() {
	// --- getSineVector: sweep phase increment (pitch) and consecutive vectors ---
	for (uint32_t inc = 1500000u; inc < 4000000000u; inc += 97000003u) {
		uint32_t phase = 0xDEADBEEFu;
		for (int rep = 0; rep < 4; ++rep) {
			auto v = SineOsc::getSineVector(&phase, inc).to_array();
			printf("gsv %u %d %d %d %d %u\n", inc, v[0], v[1], v[2], v[3], phase);
		}
	}

	// --- doFMVector: phase vector + modulator phase-shift vector --------------
	for (uint32_t inc = 2500000u; inc < 4000000000u; inc += 211000007u) {
		uint32_t phase = 0x12345678u;
		uint32_t shift = 0x00100000u;
		for (int rep = 0; rep < 4; ++rep) {
			auto phases = Argon<uint32_t>{phase}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, inc);
			auto shifts = Argon<uint32_t>{shift}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, 0x37CB9u);
			auto v = SineOsc::doFMVector(phases, shifts).to_array();
			printf("fmv %u %d %d %d %d\n", inc, v[0], v[1], v[2], v[3]);
			phase += inc * 4;
			shift += 0x37CB9u * 4;
		}
	}

	// --- the amplitude ops from the FM render loops ---------------------------
	for (int64_t amp = -0x7FFFFFFFLL; amp < 0x7FFFFFFFLL; amp += 0x13579BDFLL) {
		int32x4_t sine = {-2147483647, -12345678, 12345678, 2147483647};
		int32x4_t ampv = vdupq_n_s32((int32_t)amp);
		int32x4_t a = vqdmulhq_s32(ampv, sine);
		int32x4_t b = vqrdmulhq_n_s32(sine, (int32_t)amp >> 1);
		printf("amp %lld %d %d %d %d %d %d %d %d\n", (long long)amp, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
	}
	return 0;
}

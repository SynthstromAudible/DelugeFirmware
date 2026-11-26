/*
 * Copyright © 2026 Mark Adams
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

#pragma once

#include "arm_neon_shim.h"
#include "util/fixedpoint.h"
#include <cstdint>

namespace deluge::dsp {

// Vector form of (multiply_32x32_rshift32(x, amplitude) << 2), bit-exact for amplitude >= 0 (pan amplitudes are in
// [0, 2^30]). vqdmulh gives floor(2xa / 2^32), which only saturates when x == a == INT32_MIN, and halving that again
// rounds down to floor(xa / 2^32).
[[gnu::always_inline]] inline int32x4_t panAmplitudeVector(int32x4_t x, int32x4_t amplitude) {
	return vshlq_n_s32(vshrq_n_s32(vqdmulhq_s32(x, amplitude), 1), 2);
}

// stereo[2i] += (mono[i] * amplitudeL) >> 30, stereo[2i + 1] += (mono[i] * amplitudeR) >> 30, for i < numSamples.
// Same result as StereoSample::addPannedMono() per sample. Reads and writes exactly numSamples frames.
// Out of line: it runs once per unison part, and inlining its call sites cost more in I-cache misses than the call.
[[gnu::noinline]] inline void addPannedMonoToStereo(int32_t* __restrict__ stereo, const int32_t* __restrict__ mono,
                                                    int32_t numSamples, int32_t amplitudeL, int32_t amplitudeR) {
	int32x4_t ampL = vdupq_n_s32(amplitudeL);
	int32x4_t ampR = vdupq_n_s32(amplitudeR);
	int32_t i = 0;
	for (; i + 4 <= numSamples; i += 4) {
		int32x4_t in = vld1q_s32(mono + i);
		int32x4x2_t out = vld2q_s32(stereo + 2 * i);
		out.val[0] = vaddq_s32(out.val[0], panAmplitudeVector(in, ampL));
		out.val[1] = vaddq_s32(out.val[1], panAmplitudeVector(in, ampR));
		vst2q_s32(stereo + 2 * i, out);
	}
	for (; i < numSamples; i++) {
		stereo[2 * i] += multiply_32x32_rshift32(mono[i], amplitudeL) << 2;
		stereo[2 * i + 1] += multiply_32x32_rshift32(mono[i], amplitudeR) << 2;
	}
}

// stereo[2i] += (in[2i] * amplitudeL) >> 30, stereo[2i + 1] += (in[2i + 1] * amplitudeR) >> 30, for i < numSamples.
// Same result as StereoSample::addPannedStereo() per frame. Reads and writes exactly numSamples frames.
[[gnu::noinline]] inline void addPannedStereoToStereo(int32_t* __restrict__ stereo, const int32_t* __restrict__ in,
                                                      int32_t numSamples, int32_t amplitudeL, int32_t amplitudeR) {
	int32x4_t amp = vcombine_s32(vcreate_s32((uint64_t)(uint32_t)amplitudeL | ((uint64_t)(uint32_t)amplitudeR << 32)),
	                             vcreate_s32((uint64_t)(uint32_t)amplitudeL | ((uint64_t)(uint32_t)amplitudeR << 32)));
	int32_t numValues = numSamples * 2;
	int32_t i = 0;
	for (; i + 4 <= numValues; i += 4) {
		vst1q_s32(stereo + i, vaddq_s32(vld1q_s32(stereo + i), panAmplitudeVector(vld1q_s32(in + i), amp)));
	}
	if (i < numValues) { // one frame left
		stereo[i] += multiply_32x32_rshift32(in[i], amplitudeL) << 2;
		stereo[i + 1] += multiply_32x32_rshift32(in[i + 1], amplitudeR) << 2;
	}
}

} // namespace deluge::dsp

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

// shouldDoPanning()'s full-level amplitude. Every pan has it on at least one side (both, when centred).
constexpr int32_t kPanUnityAmplitude = 1073741823;

namespace detail {
// One channel of the pan: the sample itself on a unity side, otherwise (sample * amplitude) >> 30. Adding the sample
// directly differs from (multiply_32x32_rshift32(x, kPanUnityAmplitude) << 2) by -1..+5 LSB (about -172 dBFS), which
// saves three of the four vector operations for that channel.
template <bool unity>
[[gnu::always_inline]] inline int32x4_t panChannelVector(int32x4_t x, int32x4_t amplitude) {
	if constexpr (unity) {
		return x;
	}
	else {
		return panAmplitudeVector(x, amplitude);
	}
}
template <bool unity>
[[gnu::always_inline]] inline int32_t panChannel(int32_t x, int32_t amplitude) {
	if constexpr (unity) {
		return x;
	}
	else {
		return multiply_32x32_rshift32(x, amplitude) << 2;
	}
}

template <bool unityL, bool unityR>
[[gnu::always_inline]] inline void addPannedMonoToStereo(int32_t* __restrict__ stereo, const int32_t* __restrict__ mono,
                                                         int32_t numSamples, int32_t amplitudeL, int32_t amplitudeR) {
	int32x4_t ampL = vdupq_n_s32(amplitudeL);
	int32x4_t ampR = vdupq_n_s32(amplitudeR);
	int32_t i = 0;
	for (; i + 4 <= numSamples; i += 4) {
		int32x4_t in = vld1q_s32(mono + i);
		int32x4x2_t out = vld2q_s32(stereo + 2 * i);
		out.val[0] = vaddq_s32(out.val[0], panChannelVector<unityL>(in, ampL));
		out.val[1] = vaddq_s32(out.val[1], panChannelVector<unityR>(in, ampR));
		vst2q_s32(stereo + 2 * i, out);
	}
	for (; i < numSamples; i++) {
		stereo[2 * i] += panChannel<unityL>(mono[i], amplitudeL);
		stereo[2 * i + 1] += panChannel<unityR>(mono[i], amplitudeR);
	}
}

template <bool unityL, bool unityR>
[[gnu::always_inline]] inline void addPannedStereoToStereo(int32_t* __restrict__ stereo, const int32_t* __restrict__ in,
                                                           int32_t numSamples, int32_t amplitudeL, int32_t amplitudeR) {
	int32x4_t ampL = vdupq_n_s32(amplitudeL);
	int32x4_t ampR = vdupq_n_s32(amplitudeR);
	int32_t i = 0;
	for (; i + 4 <= numSamples; i += 4) {
		int32x4x2_t x = vld2q_s32(in + 2 * i);
		int32x4x2_t out = vld2q_s32(stereo + 2 * i);
		out.val[0] = vaddq_s32(out.val[0], panChannelVector<unityL>(x.val[0], ampL));
		out.val[1] = vaddq_s32(out.val[1], panChannelVector<unityR>(x.val[1], ampR));
		vst2q_s32(stereo + 2 * i, out);
	}
	for (; i < numSamples; i++) {
		stereo[2 * i] += panChannel<unityL>(in[2 * i], amplitudeL);
		stereo[2 * i + 1] += panChannel<unityR>(in[2 * i + 1], amplitudeR);
	}
}
} // namespace detail

// stereo[2i] += mono[i] * amplitudeL, stereo[2i + 1] += mono[i] * amplitudeR (amplitudes are Q2.30), for i <
// numSamples. StereoSample::addPannedMono() per sample, except that a kPanUnityAmplitude side adds the sample directly
// (see panChannelVector()). Reads and writes exactly numSamples frames. Out of line: it runs once per unison part, and
// inlining its call sites cost more in I-cache misses than the call.
[[gnu::noinline]] inline void addPannedMonoToStereo(int32_t* __restrict__ stereo, const int32_t* __restrict__ mono,
                                                    int32_t numSamples, int32_t amplitudeL, int32_t amplitudeR) {
	bool unityL = amplitudeL == kPanUnityAmplitude;
	bool unityR = amplitudeR == kPanUnityAmplitude;
	if (unityL && unityR) {
		detail::addPannedMonoToStereo<true, true>(stereo, mono, numSamples, amplitudeL, amplitudeR);
	}
	else if (unityL) {
		detail::addPannedMonoToStereo<true, false>(stereo, mono, numSamples, amplitudeL, amplitudeR);
	}
	else if (unityR) {
		detail::addPannedMonoToStereo<false, true>(stereo, mono, numSamples, amplitudeL, amplitudeR);
	}
	else {
		detail::addPannedMonoToStereo<false, false>(stereo, mono, numSamples, amplitudeL, amplitudeR);
	}
}

// stereo[2i] += in[2i] * amplitudeL, stereo[2i + 1] += in[2i + 1] * amplitudeR (amplitudes are Q2.30), for i <
// numSamples. StereoSample::addPannedStereo() per frame, except that a kPanUnityAmplitude side adds the sample
// directly. Reads and writes exactly numSamples frames.
[[gnu::noinline]] inline void addPannedStereoToStereo(int32_t* __restrict__ stereo, const int32_t* __restrict__ in,
                                                      int32_t numSamples, int32_t amplitudeL, int32_t amplitudeR) {
	bool unityL = amplitudeL == kPanUnityAmplitude;
	bool unityR = amplitudeR == kPanUnityAmplitude;
	if (unityL && unityR) {
		detail::addPannedStereoToStereo<true, true>(stereo, in, numSamples, amplitudeL, amplitudeR);
	}
	else if (unityL) {
		detail::addPannedStereoToStereo<true, false>(stereo, in, numSamples, amplitudeL, amplitudeR);
	}
	else if (unityR) {
		detail::addPannedStereoToStereo<false, true>(stereo, in, numSamples, amplitudeL, amplitudeR);
	}
	else {
		detail::addPannedStereoToStereo<false, false>(stereo, in, numSamples, amplitudeL, amplitudeR);
	}
}

} // namespace deluge::dsp

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

#include "util/fixedpoint.h"
#include <argon.hpp>
#include <array>
#include <cstdint>

namespace deluge::dsp {

// shouldDoPanning()'s full-level amplitude. Every pan has it on at least one side (both, when centred).
constexpr int32_t kPanUnityAmplitude = 1073741823;

namespace detail {
// One channel of the pan, (sample * amplitude) >> 30, or the sample itself on a unity side.
// MultiplyFixedQMax (vqdmulh) gives floor(2xa / 2^32), which only saturates when x == a == INT32_MIN, and halving that
// again rounds down to floor(xa / 2^32), so with amplitude >= 0 (pan amplitudes are in [0, 2^30]) this is exactly
// (multiply_32x32_rshift32(x, amplitude) << 2). Adding the sample directly on a unity side differs from that by
// -1..+5 LSB (about -172 dBFS), and saves three of the four vector operations for the channel.
template <bool unity>
[[gnu::always_inline]] inline Argon<int32_t> panChannel(Argon<int32_t> x, Argon<int32_t> amplitude) {
	if constexpr (unity) {
		return x;
	}
	else {
		return x.MultiplyFixedQMax(amplitude).ShiftRight<1>().ShiftLeft<2>();
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
	constexpr int32_t kLanes = Argon<int32_t>::lanes;
	// Broadcast once: with a scalar operand GCC re-inserts the amplitude into a lane on every iteration
	Argon<int32_t> const ampL{amplitudeL};
	Argon<int32_t> const ampR{amplitudeR};
	int32_t i = 0;
	for (; i + kLanes <= numSamples; i += kLanes) {
		auto in = Argon<int32_t>::Load(mono + i);
		auto [l, r] = Argon<int32_t>::LoadInterleaved<2>(stereo + 2 * i);
		argon::store_interleaved<2>(stereo + 2 * i, std::array<Argon<int32_t>, 2>{
		                                                l + panChannel<unityL>(in, ampL),
		                                                r + panChannel<unityR>(in, ampR),
		                                            });
	}
	for (; i < numSamples; i++) {
		stereo[2 * i] += panChannel<unityL>(mono[i], amplitudeL);
		stereo[2 * i + 1] += panChannel<unityR>(mono[i], amplitudeR);
	}
}

template <bool unityL, bool unityR>
[[gnu::always_inline]] inline void addPannedStereoToStereo(int32_t* __restrict__ stereo, const int32_t* __restrict__ in,
                                                           int32_t numSamples, int32_t amplitudeL, int32_t amplitudeR) {
	constexpr int32_t kLanes = Argon<int32_t>::lanes;
	// Broadcast once: with a scalar operand GCC re-inserts the amplitude into a lane on every iteration
	Argon<int32_t> const ampL{amplitudeL};
	Argon<int32_t> const ampR{amplitudeR};
	int32_t i = 0;
	for (; i + kLanes <= numSamples; i += kLanes) {
		auto [inL, inR] = Argon<int32_t>::LoadInterleaved<2>(in + 2 * i);
		auto [l, r] = Argon<int32_t>::LoadInterleaved<2>(stereo + 2 * i);
		argon::store_interleaved<2>(stereo + 2 * i, std::array<Argon<int32_t>, 2>{
		                                                l + panChannel<unityL>(inL, ampL),
		                                                r + panChannel<unityR>(inR, ampR),
		                                            });
	}
	for (; i < numSamples; i++) {
		stereo[2 * i] += panChannel<unityL>(in[2 * i], amplitudeL);
		stereo[2 * i + 1] += panChannel<unityR>(in[2 * i + 1], amplitudeR);
	}
}
} // namespace detail

// stereo[2i] += mono[i] * amplitudeL, stereo[2i + 1] += mono[i] * amplitudeR (amplitudes are Q2.30), for i <
// numSamples. StereoSample::addPannedMono() per sample, except that a kPanUnityAmplitude side adds the sample directly
// (see panChannel()). Reads and writes exactly numSamples frames. Out of line: it runs once per unison part, and
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

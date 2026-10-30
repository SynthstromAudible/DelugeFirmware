/*
 * Derived from Dexed's dx7note, under its licence:
 *
 * Copyright 2016-2017 Pascal Gauthier.
 * Copyright 2012 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <math.h> // NOLINT(*-deprecated-headers): the unqualified libm calls are the overloads the records pin

/// @file
/// @brief A DX7 voice's note-on scaling: pitch from the note and the op's frequency bytes, output level from the
/// keyboard and velocity, and rate from the keyboard.
namespace deluge::dsp::fixed::dx7 {

/// @brief Each coarse ratio (0 is 0.5, 1 to 31 the ratio itself) as Q24 octaves.
inline constexpr std::array<int32_t, 32> kCoarseMul{
    -16777216, 0,        16777216, 26591258, 33554432, 38955489, 43368474, 47099600, 50331648, 53182516, 55732705,
    58039632,  60145690, 62083076, 63876816, 65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858,
    74816848,  75892776, 76922906, 77910978, 78860292, 79773775, 80654032, 81503396, 82323963, 83117622};

/// @brief Each amplitude-modulation sensitivity (0 to 3) as a Q24 depth: 0, 66, 109 and 255 of 255.
inline constexpr std::array<uint32_t, 4> kAmpModSens{0, 4342338, 7171437, 16777216};

/// @brief Each pitch-modulation sensitivity (0 to 7) as a depth out of 255.
inline constexpr std::array<uint8_t, 8> kPitchModSens{0, 10, 20, 33, 55, 92, 153, 255};

/// @brief The velocity curve, by velocity / 2, in microsteps above the floor.
inline constexpr std::array<uint8_t, 64> kVelocity{
    0,   70,  86,  97,  106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181,
    184, 186, 189, 190, 194, 196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229,
    230, 232, 233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};

/// @brief The exponential keyboard-level curve, by group of three keys from the break point.
inline constexpr std::array<uint8_t, 33> kExpScale{0,  1,   2,   3,   4,   5,   6,   7,   8,   9,   11,
                                                   14, 16,  19,  23,  27,  33,  39,  47,  56,  66,  80,
                                                   94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};

/// @brief The pitch of MIDI note @p note, Q24 octaves above 1 Hz.
[[nodiscard]] constexpr int32_t note_to_log_freq(int32_t note) {
	constexpr int32_t kBase = 50857777; // (1 << 24) * (log(440) / log(2) - 69/12)
	constexpr int32_t kStep = (1 << 24) / 12;
	return kBase + (kStep * note);
}

/// @brief The output level change for @p velocity (0 to 127) at @p sensitivity (0 to 7), in microsteps.
[[nodiscard]] constexpr int32_t scale_velocity(int32_t velocity, int32_t sensitivity) {
	const int32_t clamped = std::max(0, std::min(127, velocity));
	const int32_t value = kVelocity[clamped >> 1] - 239;
	return ((sensitivity * value + 7) >> 3) << 4;
}

/// @brief The rate change for @p midinote at keyboard-rate @p sensitivity (0 to 7), in qRate units.
[[nodiscard]] constexpr int32_t scale_rate(int32_t midinote, int32_t sensitivity) {
	const int32_t x = std::min(31, std::max(0, (midinote / 3) - 7));
	return (sensitivity * x) >> 3;
}

/// @brief The level change @p group groups of three keys from the break point, at @p depth (0 to 99) on @p curve
/// (0 −linear, 1 −exponential, 2 +exponential, 3 +linear).
[[nodiscard]] constexpr int32_t scale_curve(int32_t group, int32_t depth, int32_t curve) {
	int32_t scale = 0;
	if (curve == 0 || curve == 3) {
		scale = (group * depth * 329) >> 12;
	}
	else {
		const int32_t raw = kExpScale[std::min(group, static_cast<int32_t>(kExpScale.size()) - 1)];
		scale = (raw * depth * 329) >> 15;
	}
	return curve < 2 ? -scale : scale;
}

/// @brief The keyboard level scaling for @p midinote about @p break_pt, each side at its own depth and curve.
[[nodiscard]] constexpr int32_t scale_level(int32_t midinote, int32_t break_pt, int32_t left_depth, int32_t right_depth,
                                            int32_t left_curve, int32_t right_curve) {
	const int32_t offset = midinote - break_pt - 17;
	if (offset >= 0) {
		return scale_curve((offset + 1) / 3, right_depth, right_curve);
	}
	return scale_curve(-(offset - 1) / 3, left_depth, left_curve);
}

/// @brief An op's pitch offset, Q24 octaves: a ratio (mode 0, from coarse, fine and the detune, which scales with
/// the note's pitch and takes the voice's random detune) or a fixed frequency (mode 1, from coarse, fine and detune).
/// @param log_freq_for_detune The note's pitch, Q24 octaves, which scales the ratio's detune.
/// @param random_detune The voice's draw for this op, the noise's top 16 bits.
/// @param random_detune_scale The patch's random-detune amount.
/// @param mode 0 ratio, 1 fixed frequency.
/// @param coarse The ratio (0 is 0.5, else 1 to 31) or, fixed, the decade (low two bits: 1, 10, 100, 1000 Hz).
/// @param fine 0 to 99: the ratio times 1 + fine/100, or the fixed frequency times 10^(fine/100).
/// @param detune 0 to 14, 7 none.
[[nodiscard]] inline int32_t osc_freq(int32_t log_freq_for_detune, int32_t mode, int32_t coarse, int32_t fine,
                                      int32_t detune, int32_t random_detune, int32_t random_detune_scale) {
	int32_t logfreq = 0;
	if (mode == 0) {
		// measured on a DX7
		double detune_ratio = 0.0209 * exp(-0.396 * (static_cast<float>(log_freq_for_detune) / (1 << 24))) / 7;
		int32_t random_scaled = (random_detune * random_detune_scale) >> 17;
		logfreq = static_cast<int32_t>(logfreq + (detune_ratio * log_freq_for_detune * (detune - 7 + random_scaled)));

		logfreq += kCoarseMul[coarse & 31];
		if (fine) {
			// (1 << 24) / log(2)
			logfreq += static_cast<int32_t>(floor((24204406.323123 * log(1 + (0.01 * fine))) + 0.5));
		}
	}
	else {
		// ((1 << 24) * log(10) / log(2) * .01) << 3
		logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
		logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
	}
	return logfreq;
}

} // namespace deluge::dsp::fixed::dx7

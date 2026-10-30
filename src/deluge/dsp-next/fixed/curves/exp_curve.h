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

#include "dsp-next/core/q31_ops.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"

#include <array>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief One octave of 2^x, from 2^15 to 2^16 (unsigned Q15) in 256 steps plus one.
// clang-format off
alignas(16) inline constexpr std::array<uint16_t, 257> kExpTable = {
32768, 32857, 32946, 33035, 33125, 33215, 33305, 33395, 33486, 33576, 33667, 33759, 33850, 33942, 34034, 34126,
34219, 34312, 34405, 34498, 34591, 34685, 34779, 34874, 34968, 35063, 35158, 35253, 35349, 35445, 35541, 35637,
35734, 35831, 35928, 36025, 36123, 36221, 36319, 36417, 36516, 36615, 36715, 36814, 36914, 37014, 37114, 37215,
37316, 37417, 37518, 37620, 37722, 37824, 37927, 38030, 38133, 38236, 38340, 38444, 38548, 38653, 38757, 38863,
38968, 39074, 39180, 39286, 39392, 39499, 39606, 39714, 39821, 39929, 40037, 40146, 40255, 40364, 40473, 40583,
40693, 40804, 40914, 41025, 41136, 41248, 41360, 41472, 41584, 41697, 41810, 41923, 42037, 42151, 42265, 42380,
42495, 42610, 42726, 42841, 42958, 43074, 43191, 43308, 43425, 43543, 43661, 43780, 43898, 44017, 44137, 44256,
44376, 44497, 44617, 44738, 44859, 44981, 45103, 45225, 45348, 45471, 45594, 45718, 45842, 45966, 46091, 46216,
46341, 46467, 46593, 46719, 46846, 46973, 47100, 47228, 47356, 47484, 47613, 47742, 47871, 48001, 48131, 48262,
48393, 48524, 48655, 48787, 48920, 49052, 49185, 49319, 49452, 49586, 49721, 49856, 49991, 50126, 50262, 50399,
50535, 50672, 50810, 50947, 51085, 51224, 51363, 51502, 51642, 51782, 51922, 52063, 52204, 52346, 52488, 52630,
52773, 52916, 53059, 53203, 53347, 53492, 53637, 53782, 53928, 54074, 54221, 54368, 54515, 54663, 54811, 54960,
55109, 55258, 55408, 55558, 55709, 55860, 56012, 56163, 56316, 56468, 56622, 56775, 56929, 57083, 57238, 57393,
57549, 57705, 57861, 58018, 58176, 58333, 58491, 58650, 58809, 58968, 59128, 59289, 59449, 59611, 59772, 59934,
60097, 60260, 60423, 60587, 60751, 60916, 61081, 61247, 61413, 61579, 61746, 61914, 62081, 62250, 62419, 62588,
62757, 62928, 63098, 63269, 63441, 63613, 63785, 63958, 64132, 64306, 64480, 64655, 64830, 65006, 65182, 65359,
65535, };
// clang-format on

/// @brief The exponential curve every param is shaped by: @p preset scaled by 2^(adjustment / 2^26 + 2), saturating.
/// The low 26 bits of @p adjustment are the fraction of an octave, looked up in @ref kExpTable.
[[nodiscard]] inline int32_t exp_curve(int32_t preset, int32_t adjustment) {
	const int32_t octaves = (adjustment >> 26) + 2;
	const int32_t within_octave = multiply_32x32_rshift32(
	    preset, kLinearTableInterpolator.interpolate(adjustment & 67108863, 26, kExpTable.data()));
	return increase_magnitude_and_saturate(within_octave, octaves);
}

} // namespace deluge::dsp::fixed

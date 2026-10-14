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

#include <array>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief Tanh over the full Q31 range in 256 steps plus one, Q15 (±32767): the plain (non-antialiased) curve
/// @ref tanh reads.
// clang-format off
alignas(16) inline constexpr std::array<int16_t, 257> kTanhSmallTable = {
-32767, -32766, -32764, -32762, -32761, -32759, -32757, -32755, -32753, -32750, -32748, -32745, -32742, -32739, -32736, -32733,
-32729, -32725, -32721, -32717, -32712, -32707, -32702, -32697, -32691, -32684, -32677, -32670, -32663, -32655, -32646, -32637,
-32627, -32616, -32605, -32594, -32581, -32568, -32553, -32538, -32522, -32505, -32487, -32467, -32447, -32425, -32401, -32377,
-32350, -32322, -32292, -32260, -32227, -32191, -32152, -32112, -32069, -32023, -31974, -31922, -31867, -31808, -31746, -31680,
-31610, -31535, -31456, -31372, -31282, -31187, -31087, -30980, -30867, -30747, -30619, -30484, -30341, -30190, -30029, -29859,
-29679, -29488, -29287, -29074, -28849, -28611, -28359, -28094, -27814, -27519, -27208, -26881, -26536, -26174, -25792, -25392,
-24972, -24531, -24069, -23586, -23080, -22552, -22000, -21425, -20826, -20202, -19555, -18882, -18185, -17463, -16717, -15946,
-15152, -14335, -13495, -12633, -11750, -10847, -9926, -8986, -8031, -7060, -6077, -5082, -4077, -3065, -2047, -1024,
0, 1024, 2047, 3065, 4077, 5082, 6077, 7060, 8031, 8986, 9926, 10847, 11750, 12633, 13495, 14335,
15152, 15946, 16717, 17463, 18185, 18882, 19555, 20202, 20826, 21425, 22000, 22552, 23080, 23586, 24069, 24531,
24972, 25392, 25792, 26174, 26536, 26881, 27208, 27519, 27814, 28094, 28359, 28611, 28849, 29074, 29287, 29488,
29679, 29859, 30029, 30190, 30341, 30484, 30619, 30747, 30867, 30980, 31087, 31187, 31282, 31372, 31456, 31535,
31610, 31680, 31746, 31808, 31867, 31922, 31974, 32023, 32069, 32112, 32152, 32191, 32227, 32260, 32292, 32322,
32350, 32377, 32401, 32425, 32447, 32467, 32487, 32505, 32522, 32538, 32553, 32568, 32581, 32594, 32605, 32616,
32627, 32637, 32646, 32655, 32663, 32670, 32677, 32684, 32691, 32697, 32702, 32707, 32712, 32717, 32721, 32725,
32729, 32733, 32736, 32739, 32742, 32745, 32748, 32750, 32753, 32755, 32757, 32759, 32761, 32762, 32764, 32766,
32767, };
// clang-format on

} // namespace deluge::dsp::fixed

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

/// @file
/// @brief The pitch ratios across ±50 cents, the table a fine tune reads.

namespace deluge::dsp::fixed::tables {

/// @brief 2^(c/1200) for c from −50 to +50 cents in 256 steps, plus the end entry an interpolating read at step 255
/// blends toward.
/// @details UQ1.15: 32768 is a ratio of 1.0. Entry i is the ratio of (i − 128) × 100/256 cents, so entry 0 is
/// 2^(−50/1200) and entry 256 is 2^(50/1200). 16-byte aligned for NEON lane loads.
alignas(16) inline constexpr std::array<uint16_t, 257> kCentAdjust{
    31835, 31842, 31850, 31857, 31864, 31871, 31878, 31885, 31893, 31900, 31907, 31914, 31921, 31929, 31936, 31943,
    31950, 31958, 31965, 31972, 31979, 31986, 31994, 32001, 32008, 32015, 32022, 32030, 32037, 32044, 32051, 32059,
    32066, 32073, 32080, 32088, 32095, 32102, 32109, 32117, 32124, 32131, 32138, 32146, 32153, 32160, 32167, 32175,
    32182, 32189, 32196, 32204, 32211, 32218, 32225, 32233, 32240, 32247, 32255, 32262, 32269, 32276, 32284, 32291,
    32298, 32306, 32313, 32320, 32327, 32335, 32342, 32349, 32357, 32364, 32371, 32378, 32386, 32393, 32400, 32408,
    32415, 32422, 32430, 32437, 32444, 32452, 32459, 32466, 32474, 32481, 32488, 32496, 32503, 32510, 32518, 32525,
    32532, 32540, 32547, 32554, 32562, 32569, 32576, 32584, 32591, 32598, 32606, 32613, 32620, 32628, 32635, 32643,
    32650, 32657, 32665, 32672, 32679, 32687, 32694, 32702, 32709, 32716, 32724, 32731, 32738, 32746, 32753, 32761,
    32768, 32775, 32783, 32790, 32798, 32805, 32812, 32820, 32827, 32835, 32842, 32849, 32857, 32864, 32872, 32879,
    32887, 32894, 32901, 32909, 32916, 32924, 32931, 32938, 32946, 32953, 32961, 32968, 32976, 32983, 32991, 32998,
    33005, 33013, 33020, 33028, 33035, 33043, 33050, 33058, 33065, 33073, 33080, 33087, 33095, 33102, 33110, 33117,
    33125, 33132, 33140, 33147, 33155, 33162, 33170, 33177, 33185, 33192, 33200, 33207, 33215, 33222, 33230, 33237,
    33245, 33252, 33260, 33267, 33275, 33282, 33290, 33297, 33305, 33312, 33320, 33327, 33335, 33342, 33350, 33357,
    33365, 33372, 33380, 33387, 33395, 33403, 33410, 33418, 33425, 33433, 33440, 33448, 33455, 33463, 33470, 33478,
    33486, 33493, 33501, 33508, 33516, 33523, 33531, 33538, 33546, 33554, 33561, 33569, 33576, 33584, 33591, 33599,
    33607, 33614, 33622, 33629, 33637, 33645, 33652, 33660, 33667, 33675, 33683, 33690, 33698, 33705, 33713, 33721,
    33728,
};

} // namespace deluge::dsp::fixed::tables

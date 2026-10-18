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

#include <cstdint>

namespace deluge::engine {

/// @brief A sidechain's attack rate for each step of its menu, 0..50: the stored attack is the entry shifted left by
/// 2. Falls from the slowest (step 0) to the fastest.
alignas(16) inline constexpr int32_t kSidechainAttackRates[51] = {
    262144, 221969, 187951, 159147, 134757, 114105, 96618, 81811, 69273, 58656, 49667, 42055, 35610,
    30153,  25532,  21619,  18306,  15500,  13125,  11113, 9410,  7968,  6747,  5713,  4837,  4096,
    3468,   2937,   2487,   2106,   1783,   1510,   1278,  1082,  917,   776,   657,   556,   471,
    399,    338,    286,    242,    205,    174,    147,   124,   105,   89,    76,    64,
};

/// @brief A sidechain's release rate for each step of its menu, 0..50: the stored release is the entry shifted left by
/// 3. Falls from the slowest (step 0) to the fastest.
alignas(16) inline constexpr int32_t kSidechainReleaseRates[51] = {
    32691, 4604, 2444, 1648, 1234, 980, 809, 685, 592, 519, 460, 412, 372, 338, 309, 283, 261,
    241,   224,  208,  194,  181,  169, 159, 149, 140, 132, 124, 117, 110, 104, 98,  93,  88,
    83,    78,   74,   70,   66,   62,  59,  56,  53,  50,  47,  44,  41,  39,  36,  34,  32,
};

} // namespace deluge::engine

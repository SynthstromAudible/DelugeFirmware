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
/// @brief The release-rate table envelope decays, releases and portamento read.

namespace deluge::dsp::fixed::tables {

/// @brief Envelope stage speeds for a release-table param: entry i is the rate at word (i − 32) · 2^24, 65 entries
/// with the endpoint. 16-byte aligned.
// clang-format off
alignas(16) inline constexpr std::array<int32_t, 65> kReleaseRate{
1959518848, 240577040, 126456408, 84972672, 63518640, 50408868, 41567836, 35202512,
30400724, 26649400, 23637820, 21166812, 19102814, 17352918, 15850494, 14546511,
13404089, 12394955, 11497068, 10692993, 9968758, 9313033, 8716538, 8171592,
7671792, 7211749, 6786901, 6393359, 6027784, 5687299, 5369406, 5071928,
4792960, 4530827, 4284050, 4051316, 3831462, 3623446, 3426337, 3239300,
3061582, 2892503, 2731448, 2577859, 2431229, 2291096, 2157036, 2028664,
1905625, 1787593, 1674270, 1565377, 1460662, 1359888, 1262836, 1169305,
1079105, 992062, 908014, 826807, 748301, 672363, 598870, 527703,
458757,
};
// clang-format on

} // namespace deluge::dsp::fixed::tables

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
/// @brief The manual time-stretch adjustment the sample source applies to a sample's time-stretch ratio.

namespace deluge::synth {

/// @brief The factor a manual time-stretch amount scales the time-stretch ratio by, indexed by the amount + 48.
/// @details UQ8.24: 2^24 is 1.0 (unchanged), the ratio being multiplied by the entry and shifted down 24. The 97
/// entries cover amounts -48 to +48; the rest of the 193 are zero, as they always were. 16-byte aligned.
alignas(16) inline constexpr std::array<int32_t, 193> time_stretch_adjust{
    1048576,   1110928,   1176987,   1246974,   1321123,   1399681,   1482910,   1571089,   1664511,   1763488,
    1868350,   1979448,   2097152,   2221855,   2353974,   2493948,   2642246,   2799362,   2965821,   3142178,
    3329021,   3526975,   3736700,   3958896,   4194304,   4443710,   4707947,   4987896,   5284492,   5598724,
    5931642,   6284355,   6658043,   7053950,   7473400,   7917792,   8388608,   8887421,   9415894,   9975792,
    10568984,  11197448,  11863283,  12568711,  13316085,  14107901,  14946800,  15835583,  16777216,  17774841,
    18831788,  19951585,  21137968,  22394897,  23726566,  25137421,  26632170,  28215802,  29893600,  31671166,
    33554432,  35549682,  37663576,  39903169,  42275935,  44789793,  47453133,  50274843,  53264341,  56431603,
    59787201,  63342333,  67108864,  71099365,  75327153,  79806339,  84551870,  89579586,  94906266,  100549686,
    106528681, 112863206, 119574402, 126684666, 134217728, 142198729, 150654306, 159612677, 169103741, 179159172,
    189812531, 201099372, 213057363, 225726413, 239148804, 253369332, 268435456,
};

} // namespace deluge::synth

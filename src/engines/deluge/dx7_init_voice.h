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

namespace deluge::instrument {

/// @brief The voice a new DX7 source starts with ("INIT VOICE"): the 156 bytes of a DX7 single-voice dump.
inline constexpr std::array<uint8_t, 156> kDx7InitVoice{
    99, 99, 99, 99, 99, 99, 99, 00, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  7,  99, 99, 99, 99, 99,
    99, 99, 00, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  7,  99, 99, 99, 99, 99, 99, 99, 00, 0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  7,  99, 99, 99, 99, 99, 99, 99, 00, 0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  1,  0,  7,  99, 99, 99, 99, 99, 99, 99, 00, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,
    7,  99, 99, 99, 99, 99, 99, 99, 00, 0,  0,  0,  0,  0,  0,  0,  0,  99, 0,  1,  0,  7,  99, 99, 99, 99,
    50, 50, 50, 50, 0,  0,  1,  35, 0,  0,  0,  1,  0,  3,  24, 73, 78, 73, 84, 32, 86, 79, 73, 67, 69, 63};

} // namespace deluge::instrument

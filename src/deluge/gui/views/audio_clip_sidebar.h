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

/// @file
/// @brief An audio clip's sidebar, as its legacy views draw it.

#include "board_layout.hpp"
#include "gui/colour/colour.h"

#include <cstdint>

/// @brief An audio clip's sidebar: both columns black, with the macros in the first while SESSION is held.
/// @return True: drawn.
bool renderAudioClipSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

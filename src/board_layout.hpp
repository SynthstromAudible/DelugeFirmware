/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

/// board_layout.hpp — board physical layout (pad grid, side bar, indicators,
/// numeric display).
///
/// HAL-free shared configuration: the C++ (constexpr) counterpart to the C
/// board_config.h. Both the application and the BSP may include it, so neither
/// has to reach across the boundary for these board dimensions.
#pragma once
#include <cstddef>
#include <cstdint>

// Display information (actually pads, not the display proper)
constexpr int32_t kDisplayHeight = 8; // pad-grid rows
constexpr int32_t kDisplayWidth = 16; // main pad-grid columns

constexpr int32_t kSideBarWidth = 2; // side-bar columns

constexpr int32_t kNumericDisplayLength = 4; // 7-segment digit count
constexpr size_t kNumGoldKnobIndicatorLEDs = 4;

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
/// @brief The Deluge's board profile for the controls layer, from the board's own dimensions.

#include "board_layout.hpp"
#include "ui/controls/board.h"

/// @brief The Deluge: eight mod buttons, two pressable gold knobs, the 16x8 pad grid and its two-column sidebar.
inline constexpr deluge::ui::controls::Board kDelugeControls{.sections = kNumModButtons,
                                                             .slots = kNumPhysicalModKnobs,
                                                             .slot_press = true,
                                                             .pad_columns = kDisplayWidth,
                                                             .pad_rows = kDisplayHeight,
                                                             .sidebar_columns = kSideBarWidth,
                                                             .indicator_leds = kNumGoldKnobIndicatorLEDs};

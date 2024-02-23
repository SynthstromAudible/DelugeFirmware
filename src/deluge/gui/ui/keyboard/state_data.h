/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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
#include "definitions_cxx.hpp"
#include "storage/flash_storage.h"

namespace deluge::gui::ui::keyboard {

constexpr int32_t kDefaultIsometricRowInterval = 5;
struct KeyboardStateIsomorphic {
	int32_t scrollOffset = (60 - (kDisplayHeight >> 2) * kDefaultIsometricRowInterval);
	int32_t rowInterval = kDefaultIsometricRowInterval;
};

struct KeyboardStateDrums {
	int32_t scrollOffset = 0;
	int32_t edgeSize = 4;
};

constexpr int32_t kDefaultInKeyRowInterval = 3;
struct KeyboardStateInKey {
	// Default scales have 7 elements, multipled by three octaves gives us C1 as first pad
	int32_t scrollOffset = (7 * 3);
	int32_t rowInterval = kDefaultInKeyRowInterval;
};

struct KeyboardStateSnake {
	int32_t scrollOffset = 0;
	int32_t edgeSize = 4;
};

/// Please note that saving and restoring currently needs to be added manually in instrument_clip.cpp and all layouts
/// share one struct for storage
struct KeyboardState {
	KeyboardLayoutType currentLayout = FlashStorage::defaultKeyboardLayout;

	KeyboardStateIsomorphic isomorphic;
	KeyboardStateDrums drums;
	KeyboardStateInKey inKey;
	KeyboardStateSnake snake;
};

}; // namespace deluge::gui::ui::keyboard

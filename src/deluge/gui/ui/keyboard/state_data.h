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

namespace keyboard {

constexpr int kDefaultRowInterval = 5;

enum KeyboardLayoutType : uint32_t {
	Isomorphic,
	Drums,
	InKey,
	MaxElement // Keep as boundary
};

struct KeyboardStateIsomorphic {
	int scrollOffset = (60 - (kDisplayHeight >> 2) * kDefaultRowInterval);
	int rowInterval = kDefaultRowInterval;
};

struct KeyboardStateDrums {
	int scrollOffset = 0;
	int edgeSize = 4;
};

struct KeyboardStateInKey {                                                //@TODO: Implement storage
	int scrollOffset = (60 - (kDisplayHeight >> 2) * kDefaultRowInterval); //@TODO: Select useful default
	int rowInterval = kDefaultRowInterval;
};

/// Please note that saving and restoring currently needs to be added manually in instrument_clip.cpp and all layouts share one struct for storage
struct KeyboardState {
	KeyboardLayoutType currentLayout = KeyboardLayoutType::Isomorphic;

	KeyboardStateIsomorphic isomorphic;
	KeyboardStateDrums drums;
	KeyboardStateInKey inKey;
};

}; // namespace keyboard

/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "model/scale/musical_key.h"
#include "model/scale/preset_scales.h"
#include <bitset>

namespace deluge::project {

/// Scale/key settings of a song: the current key (root note + mode notes) and which preset scales
/// are disabled. A component of deluge::project::Song. (The user-scale helper/mapper still lives on
/// the controller for now; it moves in during the idiomatic-refinement pass.)
struct ScaleSettings {
	MusicalKey key;
	std::bitset<NUM_PRESET_SCALES> disabledPresetScales;
};

} // namespace deluge::project
